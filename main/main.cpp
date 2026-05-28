#include <stdio.h>
#include <stdint.h>

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

// Edge Impulse (as 3 linhas que voce ja usava + o porting p/ ei_printf)
#include "ei_classifier_porting.h"
#include "edge-impulse-sdk/classifier/ei_model_types.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include "model-parameters/model_metadata.h"

using namespace ei;

extern "C" EI_IMPULSE_ERROR
run_classifier(ei::signal_t *signal, ei_impulse_result_t *result, bool debug);

static bool debug_nn = false;


#if EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME != 1
#warning "Este runner foi feito para modelos de AUDIO (1 amostra por frame)."
#endif

// ---------- Configuracao do ADC / microfone ----------
#define CAPTURE_CHANNEL   0              // ADC0 = GPIO26
#define ADC_CLOCK_HZ      48000000.0f



#define CONFIDENCE_THRESHOLD 0.6f       // so acende se a confianca passar disso

// A janela (numero de amostras) e a taxa de amostragem vem DIRETO do
// modelo do Edge Impulse
#define CAPTURE_DEPTH     ((int)EI_CLASSIFIER_RAW_SAMPLE_COUNT)
#define SAMPLE_RATE_HZ    ((float)EI_CLASSIFIER_FREQUENCY)

// Buffer de captura em 8 bits (1 byte por amostra -> poupa MUITA RAM).
static uint8_t capture_buf[CAPTURE_DEPTH];

// ------------------------------------------------------------------
// Callback do sinal para o classificador.
//
// O modelo foi treinado com os valores CRUS do ADC de 8 bits 
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


    adc_fifo_setup(
        true,   // grava cada conversao na FIFO
        true,   // habilita DREQ (pedido de DMA)
        1,      // DREQ disparado quando ha >= 1 amostra
        false,  // sem bit ERR (leitura de 8 bits)
        true    // empacota cada amostra em 8 bits
    );

    // Fs = clock do ADC / (clkdiv + 1)
    adc_set_clkdiv((ADC_CLOCK_HZ / SAMPLE_RATE_HZ) - 1.0f);



    ei_printf("Audio inferencing pronto - janela: %d amostras @ %.0f Hz\n",
              CAPTURE_DEPTH, SAMPLE_RATE_HZ);

    while (true) {



        // --- Captura de uma janela via DMA ---
        uint dma_chan = dma_claim_unused_channel(true);
        dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
        channel_config_set_read_increment(&cfg, false);  // le sempre da FIFO
        channel_config_set_write_increment(&cfg, true);   // escreve incrementando
        channel_config_set_dreq(&cfg, DREQ_ADC);          // ritmo dado pelo ADC


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

        






        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    stdio_init_all();

    xTaskCreate(inference_task, "inference_task", 8192, NULL, 1, NULL);
    vTaskStartScheduler();

    while (true)
        ;
}
