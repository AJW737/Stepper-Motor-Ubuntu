// main/main.c
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "STEPPER_CTRL";

// UART Configuration for Orange Pi communication
// NOTE: For testing, using UART_NUM_0 (USB console)
// Change back to UART_NUM_1 for production with Orange Pi
#define UART_NUM UART_NUM_0
#define TXD_PIN UART_PIN_NO_CHANGE  // Use default USB pins
#define RXD_PIN UART_PIN_NO_CHANGE
#define UART_BUF_SIZE 1024
#define BAUD_RATE 115200

// Stepper Motor Pin Definitions (4 motors x 4 pins each)
// X Motor
#define X_PIN1 GPIO_NUM_19
#define X_PIN2 GPIO_NUM_20
#define X_PIN3 GPIO_NUM_21
#define X_PIN4 GPIO_NUM_47

// Y Motor
#define Y_PIN1 GPIO_NUM_8
#define Y_PIN2 GPIO_NUM_9
#define Y_PIN3 GPIO_NUM_10
#define Y_PIN4 GPIO_NUM_11

// Fine Zoom Motor
#define FINE_PIN1 GPIO_NUM_12
#define FINE_PIN2 GPIO_NUM_13
#define FINE_PIN3 GPIO_NUM_14
#define FINE_PIN4 GPIO_NUM_15

// Coarse Zoom Motor
#define COARSE_PIN1 GPIO_NUM_16
#define COARSE_PIN2 GPIO_NUM_37
#define COARSE_PIN3 GPIO_NUM_41
#define COARSE_PIN4 GPIO_NUM_48

// Step timing
#define STEP_DELAY_MS 10

// Step sequence for half-stepping (smoother motion)
static const int step_sequence[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1}
};

// Motor structure
typedef struct {
    gpio_num_t pin1;
    gpio_num_t pin2;
    gpio_num_t pin3;
    gpio_num_t pin4;
    int current_step;
    int current_position;
    int min_position;
    int max_position;
    const char* name;
} Motor;

// Motor instances
static Motor x_motor = {X_PIN1, X_PIN2, X_PIN3, X_PIN4, 0, 0, -1000, 1000, "X"};
static Motor y_motor = {Y_PIN1, Y_PIN2, Y_PIN3, Y_PIN4, 0, 0, -1000, 1000, "Y"};
static Motor fine_zoom = {FINE_PIN1, FINE_PIN2, FINE_PIN3, FINE_PIN4, 0, 0, 0, 2048, "Fine Zoom"};
static Motor coarse_zoom = {COARSE_PIN1, COARSE_PIN2, COARSE_PIN3, COARSE_PIN4, 0, 0, 0, 4096, "Coarse Zoom"};

// Initialize a single motor's GPIO pins
void motor_init(Motor *motor) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << motor->pin1) | (1ULL << motor->pin2) | 
                        (1ULL << motor->pin3) | (1ULL << motor->pin4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Motor %s initialized", motor->name);
}

// Set motor step
void motor_set_step(Motor *motor, int step) {
    gpio_set_level(motor->pin1, step_sequence[step][0]);
    gpio_set_level(motor->pin2, step_sequence[step][1]);
    gpio_set_level(motor->pin3, step_sequence[step][2]);
    gpio_set_level(motor->pin4, step_sequence[step][3]);
}

// Move motor with position limits
bool motor_move(Motor *motor, int steps) {
    int direction = (steps > 0) ? 1 : -1;
    int abs_steps = (steps > 0) ? steps : -steps;
    
    // Check if move is within limits
    int new_position = motor->current_position + steps;
    if (new_position < motor->min_position || new_position > motor->max_position) {
        ESP_LOGW(TAG, "Motor %s: Move blocked - would exceed limits (current: %d, requested: %d, limits: %d to %d)", 
                 motor->name, motor->current_position, new_position, 
                 motor->min_position, motor->max_position);
        return false;
    }
    
    ESP_LOGI(TAG, "Motor %s: Moving %d steps (pos: %d -> %d)", 
             motor->name, steps, motor->current_position, new_position);
    
    for (int i = 0; i < abs_steps; i++) {
        motor_set_step(motor, motor->current_step);
        vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));
        
        motor->current_step += direction;
        if (motor->current_step > 7) motor->current_step = 0;
        if (motor->current_step < 0) motor->current_step = 7;
    }
    
    motor->current_position += steps;
    return true;
}

// Turn off motor coils
void motor_off(Motor *motor) {
    gpio_set_level(motor->pin1, 0);
    gpio_set_level(motor->pin2, 0);
    gpio_set_level(motor->pin3, 0);
    gpio_set_level(motor->pin4, 0);
}

// Initialize UART
void uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    
    // Only set pins if using UART1 (for Orange Pi)
    if (UART_NUM == UART_NUM_1) {
        uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        ESP_LOGI(TAG, "UART1 initialized on TX:%d RX:%d at %d baud", TXD_PIN, RXD_PIN, BAUD_RATE);
    } else {
        ESP_LOGI(TAG, "UART0 initialized (USB console) at %d baud - FOR TESTING ONLY", BAUD_RATE);
    }
}

// Send JSON response
void send_response(const char *status) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", status);
    char *json_string = cJSON_PrintUnformatted(response);
    
    uart_write_bytes(UART_NUM, json_string, strlen(json_string));
    uart_write_bytes(UART_NUM, "\n", 1);
    
    ESP_LOGI(TAG, "Response sent: %s", json_string);
    
    free(json_string);
    cJSON_Delete(response);
}

// Process JSON command with better validation
void process_command(const char *json_str) {
    // Validate input length
    if (strlen(json_str) > 512) {
        ESP_LOGE(TAG, "Command too long");
        send_response("error");
        return;
    }
    
    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
        }
        send_response("error");
        return;
    }
    
    cJSON *command = cJSON_GetObjectItem(json, "command");
    cJSON *amount = cJSON_GetObjectItem(json, "amount");
    
    if (!cJSON_IsString(command)) {
        ESP_LOGE(TAG, "Missing or invalid command");
        send_response("error");
        cJSON_Delete(json);
        return;
    }
    
    int steps = (amount && cJSON_IsNumber(amount)) ? amount->valueint : 1;
    
    // Validate step range to prevent extreme values
    if (steps < -10000 || steps > 10000) {
        ESP_LOGE(TAG, "Step amount out of range: %d", steps);
        send_response("error");
        cJSON_Delete(json);
        return;
    }
    
    const char *cmd = command->valuestring;
    bool success = false;
    
    ESP_LOGI(TAG, "Processing command: %s, amount: %d", cmd, steps);
    
    // Execute command
    if (strcmp(cmd, "move_x") == 0) {
        success = motor_move(&x_motor, steps);
    } else if (strcmp(cmd, "move_y") == 0) {
        success = motor_move(&y_motor, steps);
    } else if (strcmp(cmd, "zoom_in_fine") == 0) {
        success = motor_move(&fine_zoom, steps);
    } else if (strcmp(cmd, "zoom_out_fine") == 0) {
        success = motor_move(&fine_zoom, -steps);
    } else if (strcmp(cmd, "zoom_in_coarse") == 0) {
        success = motor_move(&coarse_zoom, steps);
    } else if (strcmp(cmd, "zoom_out_coarse") == 0) {
        success = motor_move(&coarse_zoom, -steps);
    } else if (strcmp(cmd, "brightness_up") == 0) {
        // Add brightness motor control here if needed
        success = true;
        ESP_LOGI(TAG, "Brightness up - not implemented");
    } else if (strcmp(cmd, "brightness_down") == 0) {
        // Add brightness motor control here if needed
        success = true;
        ESP_LOGI(TAG, "Brightness down - not implemented");
    } else if (strcmp(cmd, "aperture_up") == 0) {
        // Add aperture motor control here if needed
        success = true;
        ESP_LOGI(TAG, "Aperture up - not implemented");
    } else if (strcmp(cmd, "aperture_down") == 0) {
        // Add aperture motor control here if needed
        success = true;
        ESP_LOGI(TAG, "Aperture down - not implemented");
    } else if (strcmp(cmd, "change_lens") == 0) {
        // Add lens change motor control here if needed
        success = true;
        ESP_LOGI(TAG, "Change lens - not implemented");
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", cmd);
        send_response("invalid command");
        cJSON_Delete(json);
        return;
    }
    
    send_response(success ? "ok" : "error");
    cJSON_Delete(json);
}

// UART receive task with improved error handling
void uart_rx_task(void *arg) {
    uint8_t data[UART_BUF_SIZE];
    static char rx_buffer[UART_BUF_SIZE];
    static int rx_index = 0;
    static TickType_t last_rx_time = 0;
    
    while (1) {
        int len = uart_read_bytes(UART_NUM, data, UART_BUF_SIZE - 1, 100 / portTICK_PERIOD_MS);
        
        // Timeout protection: Clear buffer if no data for 2 seconds
        if (rx_index > 0 && (xTaskGetTickCount() - last_rx_time) > pdMS_TO_TICKS(2000)) {
            ESP_LOGW(TAG, "RX timeout - clearing incomplete message");
            rx_index = 0;
        }
        
        if (len > 0) {
            last_rx_time = xTaskGetTickCount();
            
            for (int i = 0; i < len; i++) {
                if (data[i] == '\n' || data[i] == '\r') {
                    if (rx_index > 0) {
                        rx_buffer[rx_index] = '\0';
                        ESP_LOGI(TAG, "Received: %s", rx_buffer);
                        process_command(rx_buffer);
                        rx_index = 0;
                    }
                } else if (rx_index < UART_BUF_SIZE - 2) {
                    // Only accept printable characters and common JSON chars
                    if (data[i] >= 32 && data[i] <= 126) {
                        rx_buffer[rx_index++] = data[i];
                    } else {
                        ESP_LOGW(TAG, "Invalid character received: 0x%02X", data[i]);
                    }
                } else {
                    // Buffer overflow - clear and send error
                    ESP_LOGE(TAG, "RX buffer overflow - message too long");
                    send_response("error");
                    rx_index = 0;
                    break;
                }
            }
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 Multi-Stepper Motor Controller Starting...");
    
    // Initialize all motors
    motor_init(&x_motor);
    motor_init(&y_motor);
    motor_init(&fine_zoom);
    motor_init(&coarse_zoom);
    
    // Initialize UART
    uart_init();
    
    // Start UART receive task
    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 10, NULL);
    
    ESP_LOGI(TAG, "System ready - waiting for commands...");
    
    // Main loop can handle other tasks or just idle
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
