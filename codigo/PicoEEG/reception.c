#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/adc.h"

#define UART_ID uart0
#define BAUDRATE 115200
#define NUMBER_OF_CHANNELS 4
#define NUMBER_OF_TOTAL_SAMPLES 1125
#define ADC_DELAY_US -4000

// Value for conversion
const float CONVERSION_FACTOR = 3.3f * 1000 / (1 << 12);

// Array to store values of the samples
uint16_t values_mv[NUMBER_OF_CHANNELS][NUMBER_OF_TOTAL_SAMPLES];
// Flag of sampling done
static bool sampling_done = false;

// Funcion prototypes
void init_uart(uint8_t tx_pin, uint8_t rx_pin);
void init_adc(uint8_t adc_channel_0, uint8_t adc_channel_1, uint8_t adc_channel_2, uint8_t adc_channel_3);
bool read_adc(struct repeating_timer *t);
void start_sampling(void);
void build_json(char *data, uint total_bytes);
void send_data(char *data);

int main(void) {
    // Pins for ADC
    const uint8_t ADC_PIN_CHANNEL_0 = 26;
    const uint8_t ADC_PIN_CHANNEL_1 = 27;
    const uint8_t ADC_PIN_CHANNEL_2 = 28;
    const uint8_t ADC_PIN_CHANNEL_3 = 29;

    // Pins for UART
    const uint8_t UART_TX_PIN = 0;
    const uint8_t UART_RX_PIN = 1;

    // Amount of bytes to send
    uint TOTAL_BYTES_TO_SEND = sizeof("{}\n") // Considering null character
                               + (sizeof("\"ch0\":[],") - sizeof("")) * NUMBER_OF_CHANNELS // Excluding null character
                               + (sizeof("0000,") - sizeof("")) * NUMBER_OF_TOTAL_SAMPLES * NUMBER_OF_CHANNELS // Excluding null characters
                               - (NUMBER_OF_CHANNELS + 1); // Excluding commas

    // JSON data to send
    char data_to_send[TOTAL_BYTES_TO_SEND];

    stdio_init_all();
    // Initialize UART
    init_uart(UART_TX_PIN, UART_RX_PIN);
    // Initialize ADC
    init_adc(ADC_PIN_CHANNEL_0, ADC_PIN_CHANNEL_1, ADC_PIN_CHANNEL_2, ADC_PIN_CHANNEL_3);
    // Start the sampling
    start_sampling();

    while (true) {
        if (sampling_done) {
            //printf("Sampling done\n");
            // Make the JSON to send it
            build_json(data_to_send, TOTAL_BYTES_TO_SEND);
            //printf("JSON: %s\n", data_to_send);
            // Send the JSON
            send_data(data_to_send);
            // Clear sampling flag and restart sampling
            sampling_done = false;
            start_sampling();
        }
    }

    return 0;
}

void init_uart(uint8_t tx_pin, uint8_t rx_pin) {
    // UART setup
    uart_init(UART_ID, BAUDRATE);
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);
}
void init_adc(uint8_t adc_channel_0, uint8_t adc_channel_1, uint8_t adc_channel_2, uint8_t adc_channel_3) {
    adc_init();
    // Initialize ADC channels
    adc_gpio_init(adc_channel_0);
    adc_gpio_init(adc_channel_1);
    adc_gpio_init(adc_channel_2);
    adc_gpio_init(adc_channel_3);
}

// Calback of the timer
bool read_adc(struct repeating_timer *t) {
    // Counter of sampling
    static uint sampling_count = 0;

    // Verify that the count doesn't exceed the total number of samples
    if (sampling_count >= NUMBER_OF_TOTAL_SAMPLES) {
        // Set the values after having completed the sampling
        sampling_done = true;
        cancel_repeating_timer(t);
        sampling_count = 0;
    } else {
        for (int channel = 0; channel < NUMBER_OF_CHANNELS; channel++) {
            // Read ADC channel
            adc_select_input(channel);
            uint16_t adc_value = adc_read();
            // Convert it to mV
            values_mv[channel][sampling_count] = round(adc_value * CONVERSION_FACTOR);
        }
        // Next sample
        sampling_count++;
    }
    return true;
}

void start_sampling(void) {
    // Start timer for sampling
    static struct repeating_timer timer;
    sampling_done = false;
    // Set the read_adc functon as callback with sample delay of 2000 us in order to have 500 Hz of sampling frequency
    add_repeating_timer_us(ADC_DELAY_US, read_adc, NULL, &timer);
}

// Function which transforms the adc_data to a JSON
void build_json(char *data, uint total_bytes) {
    // Stores the total number of bytes for the JSON
    char *str = (char*) malloc(total_bytes);
    if (str == NULL) {
        printf("Memory allocation failed\n");
        return;
    }

    strcpy(str, "{");
    // Print each value of the ADC channel until completing all channels with all the samplings
    for (int channel = 0; channel < NUMBER_OF_CHANNELS; channel++) {
        // Create the channel label
        char label[8];
        sprintf(label, "\"ch%d\":[", channel);

        // Concatenate the channel label to the JSON string
        strcat(str, label);

        // Create an auxiliary array
        char aux[sizeof("0000,")];

        // Print the values for the channel array
        for (int sampling_number = 0; sampling_number < NUMBER_OF_TOTAL_SAMPLES; sampling_number++) {
            if (sampling_number < NUMBER_OF_TOTAL_SAMPLES - 1) {
                sprintf(aux, "%d,", values_mv[channel][sampling_number]);
            } else {
                sprintf(aux, "%d", values_mv[channel][sampling_number]);
            }
            strcat(str, aux);
        }

        if (channel < NUMBER_OF_CHANNELS - 1) {
            strcat(str, "],");
        } else {
            strcat(str, "]}\n");
        }
    }
    strcpy(data, str);
    free(str);
}

// Send data by UART
void send_data(char *data) {
    uart_puts(UART_ID, data);
}