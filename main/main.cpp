#include <stdio.h>

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// Pico SDK
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"

// audio output
#include "hardware/irq.h"  // interrupts
#include "hardware/pwm.h"  // pwm 
#include "hardware/sync.h" // wait for interrupt 
#include "hardware/clocks.h" // redefined set_sys_clock_khz() 

// Edge Impulse (as 3 linhas que voce ja usava + o porting p/ ei_printf)
#include "ei_classifier_porting.h"
#include "edge-impulse-sdk/classifier/ei_model_types.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include "model-parameters/model_metadata.h"


// pio header
#include "ws2812.pio.h"

QueueHandle_t xQueueInferencia;



using namespace ei;

extern "C" EI_IMPULSE_ERROR
run_classifier(ei::signal_t *signal, ei_impulse_result_t *result, bool debug);

static bool debug_nn = false;

// ------------------------------------------------------------------
// Sanidade: este runner so faz sentido para um modelo de AUDIO
// (1 canal). Se voce exportou um modelo de acelerometro, isso aqui
// nao vale.
// ------------------------------------------------------------------
#if EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME != 1
#warning "Este runner foi feito para modelos de AUDIO (1 amostra por frame)."
#endif

// ---------- Configuracao do ADC / microfone ----------
#define CAPTURE_CHANNEL   0              // ADC0 = GPIO26
#define ADC_CLOCK_HZ      48000000.0f
#define LED               25            // LED indicador de gravacao (onboard no Pico padrao;
                                        // no Pico W o onboard nao e o GPIO 25 -> troque de pino)

// ---------- LEDs de saida: um por label ----------
// Sobe a partir do GPIO 16:
//   label 0 -> GPIO 16, label 1 -> GPIO 17, label 2 -> GPIO 18, label 3 -> GPIO 19, ...
#define LABEL_LED_BASE       17
#define LABEL_LED_STEP       (+1)
#define CONFIDENCE_THRESHOLD 0.6f       // so acende se a confianca passar disso

// pino do LED do label ix
#define LABEL_LED_PIN(ix)    (LABEL_LED_BASE + ((int)(ix)) * LABEL_LED_STEP)

// A janela (numero de amostras) e a taxa de amostragem vem DIRETO do
// modelo do Edge Impulse, entao isso se adapta sozinho ao seu impulse:
//   EI_CLASSIFIER_RAW_SAMPLE_COUNT -> nº de amostras da janela (ex.: 16000 p/ 1 s)
//   EI_CLASSIFIER_FREQUENCY        -> taxa de amostragem (ex.: 16000 Hz)
#define CAPTURE_DEPTH     ((int)EI_CLASSIFIER_RAW_SAMPLE_COUNT)
#define SAMPLE_RATE_HZ    ((float)EI_CLASSIFIER_FREQUENCY)


// Audio PIN is to match some of the design guide shields. 
// #define AUDIO_PIN 28  // you can change this to whatever you like
#define AUDIO_PIN 27

#define RELAY_PIN 16

#include "sample.h"
#include "somfuncao.h"
volatile int wav_position = 0;

// ==========================================
// CONFIGURAÇÕES DO NEOPIXEL (SK6812)
// ==========================================
#define LED_PIN 15       // Pino de dados ligado ao anel
#define NUM_LEDS 16      // Quantidade de LEDs no anel
#define IS_RGBW false     // Crucial: define que é um SK6812 (tem o LED branco)
#define LED_TOPO 0       // Altere esse índice para o LED que estiver fisicamente "no topo" (0 a 15)
// Buffer global para guardar as cores atuais do anel
uint32_t led_buffer[NUM_LEDS] = {0};

volatile int sound = 0;

// pico audio output
void pwm_interrupt_handler() {
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));    

    if (sound == 0){
        irq_set_enabled(PWM_IRQ_WRAP, true);
        if (wav_position < (WAV_DATA_LENGTH<<3) - 1) { 
                // set pwm level 
                // allow the pwm value to repeat for 8 cycles this is >>3 
                
                pwm_set_gpio_level(AUDIO_PIN, WAV_DATA[wav_position>>3]);  
                wav_position++;
            } else {
                // reset to start
                wav_position = 0;
            }
    }

    if (sound == 1){
        if (wav_position < (WAV_DATA_LENGTH_FUNCAO<<3) - 1) { 
        // set pwm level 
        // allow the pwm value to repeat for 8 cycles this is >>3 
        pwm_set_gpio_level(AUDIO_PIN, WAV_DATA_FUNCAO[wav_position>>3]);  
        wav_position++;
    } else {
        // reset to start
        wav_position = 0;
        irq_set_enabled(PWM_IRQ_WRAP, false);

    }
    
    }
    
}



// Formata as 4 cores em um único número de 32 bits exigido pelo SK6812


static inline uint32_t urgbw_u32(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    return
            ((uint32_t) (g) << 24) | // Verde no topo
            ((uint32_t) (r) << 16) | // Vermelho logo depois
            ((uint32_t) (b) << 8)  | // Azul em seguida
            (uint32_t) (w);          // Branco por último
}

// Envia 1 pixel para a máquina de estado do PIO
static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grbw) {
    pio_sm_put_blocking(pio, sm, pixel_grbw); // O deslocamento de bits foi removido
}

// Envia o array inteiro para o anel e dá uma micro pausa
void atualiza_anel_leds() {
    for (int i = 0; i < NUM_LEDS; i++) {
        // CORRIGIDO: Passando os 3 parâmetros exigidos pela nova função
        put_pixel(pio0, 0, led_buffer[i]); 
    }
    // Atraso de 1ms para o chip registrar os dados da fita
    vTaskDelay(pdMS_TO_TICKS(1)); 
}

// Buffer de captura em 8 bits (1 byte por amostra -> poupa MUITA RAM).
static uint8_t capture_buf[CAPTURE_DEPTH];

// ------------------------------------------------------------------
// Callback do sinal para o classificador.
//
// O modelo foi treinado com os valores CRUS do ADC de 8 bits (0..255,
// com o offset DC intacto, ~128). Entao a inferencia tem que entregar
// exatamente isso: sem centralizar, sem escalar. So converte o byte
// para float, amostra-a-amostra (sem buffer float gigante).
// ------------------------------------------------------------------
static int audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = (float)capture_buf[offset + i];   // 0..255, igual ao treino
    }
    return EIDSP_OK;
}

static void inference_task(void *pvParameters)
{
    // --- Inicializacao do ADC ---
    adc_gpio_init(26 + CAPTURE_CHANNEL);   // hi-Z, sem pull, buffer digital off
    adc_init();
    adc_select_input(CAPTURE_CHANNEL);

    adc_fifo_setup(
        true,   // grava cada conversao na FIFO
        true,   // habilita DREQ (pedido de DMA)
        1,      // DREQ disparado quando ha >= 1 amostra
        false,  // sem bit ERR (leitura de 8 bits)
        true    // empacota cada amostra em 8 bits
    );

    // Fs = clock do ADC / (clkdiv + 1)
    adc_set_clkdiv((ADC_CLOCK_HZ / SAMPLE_RATE_HZ) - 1.0f);

    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);

    // Um LED de saida por label
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        int pin = LABEL_LED_PIN(ix);
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }


    ei_printf("Audio inferencing pronto - janela: %d amostras @ %.0f Hz\n",
              CAPTURE_DEPTH, SAMPLE_RATE_HZ);
    char envio[10] = {0};


    
    while (true) {

        // --- Pequeno aviso "vou gravar" no LED ---
        for (int i = 0; i < 3; i++) {
            gpio_put(LED, 1); vTaskDelay(pdMS_TO_TICKS(120));
            gpio_put(LED, 0); vTaskDelay(pdMS_TO_TICKS(120));
        }

        // --- Captura de uma janela via DMA ---
        uint dma_chan = dma_claim_unused_channel(true);
        dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
        channel_config_set_read_increment(&cfg, false);  // le sempre da FIFO
        channel_config_set_write_increment(&cfg, true);   // escreve incrementando
        channel_config_set_dreq(&cfg, DREQ_ADC);          // ritmo dado pelo ADC

        gpio_put(LED, 1);  // LED aceso = gravando

        dma_channel_configure(dma_chan, &cfg,
                              capture_buf,    // destino
                              &adc_hw->fifo,  // origem
                              CAPTURE_DEPTH,  // quantidade
                              true);          // comeca imediatamente

        adc_run(true);
        dma_channel_wait_for_finish_blocking(dma_chan);
        adc_run(false);
        adc_fifo_drain();
        dma_channel_unclaim(dma_chan);

        gpio_put(LED, 0);

        // --- Monta o sinal apontando para o callback (sem buffer float extra) ---
        signal_t signal;
        signal.total_length = CAPTURE_DEPTH;   // == EI_CLASSIFIER_RAW_SAMPLE_COUNT
        signal.get_data     = &audio_signal_get_data;

        // --- Roda o classificador ---
        ei_impulse_result_t result = { 0 };
        EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
        if (err != EI_IMPULSE_OK) {
            ei_printf("ERR: Failed to run classifier (%d)\n", err);
            continue;
        }

        // --- Resultados ---
        ei_printf("Predictions (DSP: %d ms, NN: %d ms, anomaly: %d ms):\n",
                  result.timing.dsp,
                  result.timing.classification,
                  result.timing.anomaly);

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            ei_printf("    %s: %.5f\n",
                      result.classification[ix].label,
                      result.classification[ix].value);
        }

#if EI_CLASSIFIER_HAS_ANOMALY == 1
        ei_printf("    anomaly score: %.3f\n", result.anomaly);
#endif

        // --- Acende o LED do label vencedor (argmax), se passar do limiar ---
        size_t top_ix  = 0;
        float  top_val = 0.0f;
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            if (result.classification[ix].value > top_val) {
                top_val = result.classification[ix].value;
                top_ix  = ix;
            }
        }

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            bool on = (ix == top_ix) && (top_val >= CONFIDENCE_THRESHOLD);
            gpio_put(LABEL_LED_PIN(ix), on);
        }

        if (top_val >= CONFIDENCE_THRESHOLD) {
            ei_printf("==> %s (%.2f)\n",
                      result.classification[top_ix].label, top_val);

                    // manda pra fila de inferencia
                    // Compara o resultado direto do modelo. Se NÃO for igual a "noise", envia.
                    if (strcmp(result.classification[top_ix].label, "noise") != 0) {
                        
                        strcpy(envio, result.classification[top_ix].label);
                        xQueueSend(xQueueInferencia, envio, portMAX_DELAY);
                        
                    }
                    

        } else {
            ei_printf("==> (nenhum acima de %.2f)\n", CONFIDENCE_THRESHOLD);
        }

    }
}

void zera_led (){
    // zera cores ao começar
    for (int i = 0; i < NUM_LEDS; i++) {
                    // Calcula a posição circular para sempre começar do topo
                    int pos = (LED_TOPO + i) % NUM_LEDS; 
                    
                    // Define Azul Escuro (R=0, G=0, B=20, W=0). 
                    led_buffer[pos] = urgbw_u32(0, 0, 0, 0); 
                    
                    atualiza_anel_leds();
                    vTaskDelay(pdMS_TO_TICKS(40)); // Pausa de 40ms entre cada LED
                }
}

static void pwm_audio_task(void *pvParameters){
    // set_sys_clock_khz(176000, true); 
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    // Setup PWM interrupt to fire when PWM cycle is complete
    pwm_clear_irq(audio_pin_slice);
    pwm_set_irq_enabled(audio_pin_slice, true);
    // set the handle function above
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_interrupt_handler); 
    // começa desligado
    irq_set_enabled(PWM_IRQ_WRAP, false);

    // Setup PWM for audio output
    pwm_config config = pwm_get_default_config();
    /* Base clock 176,000,000 Hz divide by wrap 250 then the clock divider further divides
     * to set the interrupt rate. 
     * 
     * 11 KHz is fine for speech. Phone lines generally sample at 8 KHz
     * 
     * 
     * So clkdiv should be as follows for given sample rate
     *  8.0f for 11 KHz
     *  4.0f for 22 KHz
     *  2.0f for 44 KHz etc
     */
    pwm_config_set_clkdiv(&config, 6.818f); 
    pwm_config_set_wrap(&config, 250); 
    pwm_init(audio_pin_slice, &config, true);

    pwm_set_gpio_level(AUDIO_PIN, 0);
    char inference[10] = {0};
    // __wfi(); // Wait for Interrupt


    // neopixel
    // 1. Inicialização do PIO (roda apenas uma vez quando a Task começa)
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    
    // Frequência padrão de 800kHz exigida pelos NeoPixels
    ws2812_program_init(pio, sm, offset, LED_PIN, 800000, IS_RGBW);

    gpio_init(RELAY_PIN);
    gpio_set_dir(RELAY_PIN, GPIO_OUT);
    gpio_put(RELAY_PIN,1);

    zera_led();
    while(1) {
        if (xQueueReceive(xQueueInferencia, inference, portMAX_DELAY)) {
            inference[9] = '\0'; 

            // 3. Se a palavra for "daniboy" (verifica as 7 primeiras letras)
            if (strncmp(inference, "insper", 6) == 0) {
                
                // --- ANIMAÇÃO DE LIGAR ---
                for (int i = 0; i < NUM_LEDS; i++) {
                    int pos = (LED_TOPO + i) % NUM_LEDS; 
                    led_buffer[pos] = urgbw_u32(20, 0, 0, 0); // red 
                    atualiza_anel_leds();
                    vTaskDelay(pdMS_TO_TICKS(40)); 
                }
                gpio_put(RELAY_PIN,1);
                vTaskDelay(pdMS_TO_TICKS(3000));
                zera_led();
                // ==========================================
                // LIMPA A FILA AQUI PARA EVITAR "ECOS" DA IA
                xQueueReset(xQueueInferencia); 
                // ==========================================
            
                // --- ESPERA ATÉ 5 SEGUNDOS PELA SEGUNDA PALAVRA ---
                // char segunda_palavra[10] = {0};
                // if (xQueueReceive(xQueueInferencia, segunda_palavra, pdMS_TO_TICKS(5000)) == pdPASS) {
                //     // Recebeu palavra antes de 5 segundos!
                //     segunda_palavra[9] = '\0';
                //     printf("Acao abortada/concluida. Palavra recebida: %s\n", segunda_palavra);
                //     irq_set_enabled(PWM_IRQ_WRAP, true);
                //     sound = 1;

                //     // if play
                //     if (strncmp(segunda_palavra, "play", 4) == 0){
                //         sound = 0;
                //         irq_set_enabled(PWM_IRQ_WRAP, true);
                //         zera_led();
                //     }

                //     // if stop
                //     if (strncmp(segunda_palavra, "stop", 4) == 0){
                //         irq_set_enabled(PWM_IRQ_WRAP, false);
                //         zera_led();
                //     }


                     
                // } else {
                //     // Não escutou nada em 5 segundos
                //     printf("Timeout! 5 segundos se passaram.\n");
                //     // --- ANIMAÇÃO DE DESLIGAR (Reversa) ---
                //     // Vai do último LED que acendeu até o primeiro
                //     for (int i = NUM_LEDS - 1; i >= 0; i--) {
                //         int pos = (LED_TOPO + i) % NUM_LEDS; 
                        
                //         // Define como Desligado (Tudo 0)
                //         led_buffer[pos] = urgbw_u32(0, 0, 0, 0); 
                        
                //         atualiza_anel_leds();
                //         vTaskDelay(pdMS_TO_TICKS(40)); // Esvazia num ritmo de 40ms
                //     }
                // }
            }
            // if (strncmp(inference, "fortaleza", 9) == 0) {
            if (strncmp(inference, "fortaleza", 9) == 0) {
                
                // --- ANIMAÇÃO DE LIGAR ---
                for (int i = 0; i < NUM_LEDS; i++) {
                    int pos = (LED_TOPO + i) % NUM_LEDS; 
                    if (i%2){
                    led_buffer[pos] = urgbw_u32(0, 0, 20, 0); // red                         
                    }
                    else {
                    led_buffer[pos] = urgbw_u32(20, 20, 20, 0); // red                            
                    }
                    atualiza_anel_leds();
                    vTaskDelay(pdMS_TO_TICKS(40)); 
                }
                gpio_put(RELAY_PIN,0);
                vTaskDelay(pdMS_TO_TICKS(3000));
                zera_led();

                // ==========================================
                // LIMPA A FILA AQUI PARA EVITAR "ECOS" DA IA
                xQueueReset(xQueueInferencia); 

        }

        
    }

}

 }

int main(void)
{
    stdio_init_all();

    xQueueInferencia = xQueueCreate(32, sizeof(char)*10);
    // Stack generoso: a DSP de audio (MFCC/MFE) consome bastante.
    xTaskCreate(pwm_audio_task, "audio pwm", 8192, NULL, 1, NULL);
    xTaskCreate(inference_task, "inference_task", 8192, NULL, 1, NULL);
    vTaskStartScheduler();

    while (true)
        ;
}