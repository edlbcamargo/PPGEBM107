/* 
 * Exemplo de uso do ADC do STM32F103 em Dual Mode.
 *  
 * A taxa de amostragem é dada por:
 *  FS = 72 MHz / prescaler / (ADC_SMPR + 12.5)
 *  
 * Para PRE_SCALER==RCC_ADCPRE_PCLK_DIV_6 e ADC_SMPR==ADC_SMPR_1_5, a taxa de amostragem é:
 *  - Regular simultaneous mode: 857 kSps por canal, no caso de 2 canais
 *  - Fast interleaved mode: 1714 kSps
 *  
 * Para PRE_SCALER==RCC_ADCPRE_PCLK_DIV_2 e ADC_SMPR==ADC_SMPR_1_5, a taxa de amostragem é:
 *  - Regular simultaneous mode: 2.571 MSps por canal, no caso de 2 canais
 *  - Fast interleaved mode: 5.143 MSps
 *  
 * Obs: segundo nota do Reference Manual ([1], pág 215), não deve ser usado prescaler menor que 6 (72MHz/6 = 12MHz):
 * "The ADC input clock is generated from the PCLK2 clock divided by a prescaler and it must not exceed 14 MHz"
 * Provavelmente, a precisão da medida cai em frequências mais altas e cargas de alta impedância de saída devido aos 
 * capacitores do ADC (ADC do tipo "successive approximation analog-to-digital converter"). Ver [5], págs 6 e 33.
 *  
 * Escrito por: Erick León
 * 
 * Referências:
 * 
 * [1] STM32F10xxx Reference Manual (RM0008), disponível em www.st.com;
 * [2] Application note (AN3116), STM32's ADC modes and theis applications, disponível em www.st.com;
 * [3] Problems with regular simultaneous dual ADC conversion with DMA transfer, Spark Logic Forum,
 * disponível em https://sparklogic.ru/arduino-for-stm32/problems-with-regular-simultaneous-dual-adc.html
 * [4] https://github.com/rogerclarkmelbourne/Arduino_STM32/blob/master/STM32F1/libraries/STM32ADC/src/STM32ADC.cpp
 * [5] Application note (AN2834), How to get the best ADC accuracy in STM32 microcontrollers, disponível em www.st.com;

 * 
 */

#include "stm32_adc_dual_mode.h"
#include "dft.h"


#define REFERENCE_VOLTS   3.3                    // fundo de escala do ADC
#define CHANNELS_PER_ADC  1                      // number of channels for each ADC. Must match values in ADCx_Sequence array below
#define NUM_SAMPLES_MAX   300                     // number of samples for each ADCx. Each channel will be sampled NUM_SAMPLES/CHANNELS_PER_ADC
#define ADC_SMPR          ADC_SMPR_7_5           // when using dual mode, each pair of channels must have same rate. Here all channels have the same
#define PRE_SCALER        RCC_ADCPRE_PCLK_DIV_6  // Prescaler do ADC
#define FAST_INTERLEAVED  false                  // Fast Interleave Mode Flag. Para "dobrar" taxa de amostragem medindo o mesmo canal dos 2 ADCs.
                                                 // Se 'false', habilita "Regular simultaneous mode". Se 'true', habilita "Fast interleaved mode".

#define GANHO_CORRENTE 355.0 // para Rg=3,3k e Rs=22ohm -> G = (1+50k/Rg)*Rs


uint32 adcbuf[NUM_SAMPLES_MAX+1];  // buffer to hold samples, ADC1 16bit, ADC2 16 bit

// O STM32F103 possui 10 pinos do ADC disponíveis:
// pino A0 (PA0) -> 0 (ADC0)
// ...
// pino A7 (PA7) -> 7 (ADC7)
// pino B0 (PB0) -> 8 (ADC8)
// pino B1 (PB1) -> 9 (ADC9)
// Para "dobrar" taxa de amostragem (FAST_INTERLEAVED true), medir o mesmo canal dos 2 ADCs.
uint8 ADC1_Sequence[]={8,0,0,0,0,0};   // ADC1 channels sequence, left to right. Unused values must be 0. Note that these are ADC channels, not pins  
uint8 ADC2_Sequence[]={9,0,0,0,0,0};   // ADC2 channels sequence, left to right. Unused values must be 0

char comando;
int num_samples = 6;
float freq_sinal  = 100000; // 100kHz
float sample_freq = 600000; // 600kSps
int pontos_por_ciclo = 6;

float media1, media2, amplit1, amplit2, phase1, phase2;

////////////////////////////////////////////////////////////////////////////////////
void envia_medidas(){
  // medindo valores:
  start_convertion_dual_channel(adcbuf, num_samples);
  wait_convertion_dual_channel();

  // imprimindo valores lidos:
  for(int i=0;i<(num_samples);i++) {
    float volts= ((adcbuf[i] & 0xFFFF) / 4095.0)* REFERENCE_VOLTS;
    float voltss=  (((adcbuf[i] & 0xFFFF0000) >>16) / 4095.0)* REFERENCE_VOLTS;
    
    if(FAST_INTERLEAVED){ // Fast interleaved mode
      /*Serial.print("ADC:");
      Serial.println(voltss); //ADC2 é convertido primeiro... Ver [2], pág 10.
      Serial.print("ADC:");
      Serial.println(volts);*/
    }
    else{ // Regular simultaneous mode
      Serial.print("C1:");
      Serial.print(volts);
      Serial.print("\tC2:");
      Serial.println(voltss);
    }
  }
  Serial.println();
}

////////////////////////////////////////////////////////////////////////////////////
void demodula(){
  // medindo valores:
  start_convertion_dual_channel(adcbuf, num_samples);
  wait_convertion_dual_channel();

  if(FAST_INTERLEAVED){
    /*int num_samples2 = num_samples*2;
    uint16_t data_adc1[num_samples2];

    for(int i=0;i<(num_samples);i++) {
      data_adc1[2*i]   = (adcbuf[i] & 0xFFFF);
      data_adc1[2*i+1] = ((adcbuf[i] & 0xFFFF0000) >>16);
      
    }
    float media = sinal_medio (data_adc1, num_samples2);
    float amplit;
    float phase;
    calc_dft_singfreq_phase(data_adc1, freq_sinal, sample_freq*2.0, media, amplit, phase, 1000, num_samples2);
    Serial.print("Amplitude: ");
    Serial.print(amplit);
    Serial.print("V; fase: ");
    Serial.print(phase*180.0/3.141529);
    Serial.println(" graus");*/
  }
  else{
    uint16_t data_adc1[num_samples];
    uint16_t data_adc2[num_samples];

    for(int i=0;i<(num_samples);i++) {
      data_adc1[i] = (adcbuf[i] & 0xFFFF);
      data_adc2[i] = ((adcbuf[i] & 0xFFFF0000) >>16);
      
    }
    media1 = sinal_medio (data_adc1, num_samples);
    calc_dft_singfreq(data_adc1, freq_sinal, sample_freq, media1, amplit1, phase1, 1000, num_samples);
    media2 = sinal_medio (data_adc2, num_samples);
    calc_dft_singfreq(data_adc2, freq_sinal, sample_freq, media2, amplit2, phase2, 1000, num_samples);
  }
}

////////////////////////////////////////////////////////////////////////////////////
void envia_demodulacao(){
  Serial.print("A1: ");
  Serial.print(amplit1);
  Serial.print("V; f1: ");
  Serial.print(phase1*180.0/3.14153);
  Serial.println(" graus");
  Serial.print("A2: ");
  Serial.print(amplit2);
  Serial.print("V; f2: ");
  Serial.print(phase2*180.0/3.14153);
  Serial.println(" graus");
}

////////////////////////////////////////////////////////////////////////////////////
void calc_impedancia(){
  float modulo_impedancia = amplit1/(amplit2/GANHO_CORRENTE);
  float fase = phase1-phase2;
  Serial.print("Z: ");
  Serial.print(modulo_impedancia);
  Serial.print("ohm; fase: ");
  Serial.print(fase*180.0/3.14153);
  Serial.println("graus; ");
  /*float imp_real = modulo_impedancia*cos(fase);
  float imp_imag = modulo_impedancia*sin(fase);
  Serial.print(imp_real);
  Serial.print(" + i*(");
  Serial.print(imp_imag);
  Serial.println(")");*/
}


////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  set_adc_dual_channel(PRE_SCALER, ADC_SMPR, CHANNELS_PER_ADC, ADC1_Sequence, ADC2_Sequence, FAST_INTERLEAVED);  // initial ADC1 and ADC2 settings
}

////////////////////////////////////////////////////////////////////////////////////
void loop() {

  if(Serial.available()){
    comando = Serial.read();
    int frq = freq_sinal/1000;

    switch (comando) {
      case 'e':
        envia_medidas();
        break;

      case 'd':
        demodula();
        envia_demodulacao();
        break;

      case 'f':
        switch (frq) {
          case 2:
            freq_sinal = 4000;
            pontos_por_ciclo = 150;
            num_samples = 150;
            break;

          case 4:
            freq_sinal = 6000;
            pontos_por_ciclo = 100;
            num_samples = 100;
            break;

          case 6:
            freq_sinal = 12000;
            pontos_por_ciclo = 50;
            num_samples = 50;
            break;

          case 12:
            freq_sinal = 24000;
            pontos_por_ciclo = 25;
            num_samples = 25;
            break;

          case 24:
            freq_sinal = 30000;
            pontos_por_ciclo = 20;
            num_samples = 20;
            break;

          case 30:
            freq_sinal = 60000;
            pontos_por_ciclo = 10;
            num_samples = 10;
            break;

          case 60:
            freq_sinal = 100000;
            pontos_por_ciclo = 6;
            num_samples = 6;
            break;

          case 100:
            freq_sinal = 120000;
            pontos_por_ciclo = 5;
            num_samples = 5;
            break;

          case 120:
            freq_sinal = 200000;
            pontos_por_ciclo = 3;
            num_samples = 3;
            break;

          case 200:
            freq_sinal = 2000;
            pontos_por_ciclo = 300;
            num_samples = 300;
            break;

          default:
            break;
          
        }
        Serial.print("Freq ");
        Serial.print(freq_sinal);
        Serial.print("Hz; N ");
        Serial.println(num_samples);
        break;

      case 'i':
        demodula();
        calc_impedancia();
        break;

      case '+':
        if(num_samples<=NUM_SAMPLES_MAX-pontos_por_ciclo) num_samples+=pontos_por_ciclo;
        Serial.print("num_samples: ");
        Serial.println(num_samples);
        break;

      case '-':
        if(num_samples>=2*pontos_por_ciclo) num_samples-=pontos_por_ciclo;
        Serial.print("num_samples: ");
        Serial.println(num_samples);
        break;

      default:
        break;
    }
  }
  else{
    delay(10);
  }  
}
