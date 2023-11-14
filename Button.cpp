#include "Button.hpp"

//esp-idf includes
#include "esp_timer.h"
#include "esp_debug_helpers.h"

bool Button::isr_service_installed = {false};

Button::Button(button_config_t button_conf, bool logging_en, const char *name):
event(Button::ButtonEvent::quick_press), 
button_conf(button_conf), 
logging_en(logging_en), 
pending_scan(false), 
name(name)
{
    
    gpio_config_t gpio_conf;

    //error checking:
    //check to see if button gpio has been assigned
    if(button_conf.gpio_num == GPIO_NUM_NC)
    {
        //print error and dump backtrace
        ESP_LOGE(TAG, "Button not Initialized: GPIO unassigned.");
        esp_backtrace_print(10);
    }
    //check to see if button is correctly assigned as active low or high
    else if(button_conf.active_hi != !button_conf.active_lo)
    {
        //print error and dump backtrace
        ESP_LOGE(TAG, "Button not Initialized: Button must be active high or active low, cannot be both.");
        esp_backtrace_print(10);
    }
    //check to see that long-press event generation time is between 0 & 5 seconds
    else if(!((button_conf.long_press_evt_time >= 10000) && (button_conf.long_press_evt_time <= 5000000)))
    {
        //print error and dump backtrace
        ESP_LOGE(TAG, "Button not Initialized: Long press event time out of range, must be between 10000us to 5000000us");
        esp_backtrace_print(10);
    } 
    //check to see that held event generation time is between 0 & 5 seconds
    else if(!((button_conf.held_evt_time >= 10000) && (button_conf.held_evt_time <= 5000000)))
    {
        //print error and dump backtrace
        ESP_LOGE(TAG, "Button not Initialized: Held press event time out of range, must be between 10000us to 5000000us");
        esp_backtrace_print(10);
    }
    else
    {
        gpio_conf.pin_bit_mask = (0x1 << (uint16_t)button_conf.gpio_num); //assign button gpio number
        gpio_conf.mode = GPIO_MODE_INPUT; //set gpio mode as input

        //if active high button
        if(button_conf.active_hi)
        {
            gpio_conf.intr_type = GPIO_INTR_POSEDGE; //set positive edge interrupt

            //if internal pullups/pulldowns are enabled
            if(button_conf.pull_en)
            {
                //active high, internal pullup disabled, internal pulldown enabled
                gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
                gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
            }
            else 
            {
                //internal pullups/pulldowns diabled
                gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
                gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; 
            }
            
        }
        //if active low button
        else if (button_conf.active_lo)
        {
            gpio_conf.intr_type = GPIO_INTR_NEGEDGE; //set negative edge interrupt
        
            //if internal pullups/pulldowns are enabled
            if(button_conf.pull_en)
            {
                //active low, internal pullup enabled, internal pulldown disabled
                gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE; 
                gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            }
            else 
            {
                //internal pullups/pulldowns diabled
                gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
                gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; 
            }
        }

        gpio_config(&gpio_conf); //configure gpio pin
        
        if(!isr_service_installed)
        {
            gpio_install_isr_service(0); //install isr service
            isr_service_installed = true; 
        }

        ESP_ERROR_CHECK(gpio_isr_handler_add(button_conf.gpio_num, button_handler, (void *)this)); //add button isr handler

        button_task_hdl = NULL; 
        xTaskCreate(&button_task_trampoline, "button_task", 2048, this, 4, &button_task_hdl);

        if(logging_en){
            ESP_LOGI(TAG, "ButtonName: %s| GPIO[%2d]| ActiveLow: %s| ActiveHi: %s| InternalPullUpDown: %s| LongPressEvtTime: %lld us| HeldEvtTime: %lld us", 
            name, (int16_t)button_conf.gpio_num, ((button_conf.active_lo)?"true":"false"), ((button_conf.active_hi)?"true":"false"), ((button_conf.pull_en)?"true":"false"),
            button_conf.long_press_evt_time, button_conf.held_evt_time);
        }
        

    }

}

bool Button::scan()
{
    //if data has not been updated since last time scan() was called
    if(!pending_scan)
        return false;
    //data has been updated since last time scan() was called    
    else{
        pending_scan = false;
        return true; 
    }
}

 void Button::button_task(void){

    while(1){

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); //block until notified by respective GPIO ISR

        vTaskDelay(25/portTICK_PERIOD_MS); //debounce press

        //check for press type, if button is not released (long press) then check for release event
        if(!press_check())
            released_check();   

        gpio_intr_enable(button_conf.gpio_num); //re-enable interrupts to resume task if button activity is detected
    }
 }

void Button::button_task_trampoline(void *arg)
 {
    Button *local_button = (Button *)arg; //cast to button pointer

    local_button->button_task(); //launch button task

 }

bool Button::press_check()
{
    int64_t time_stamp = esp_timer_get_time();
    bool press_check = false;
    bool released = false; 

    //press check: look for long-press or quick-press event
    while(!press_check)
    {
        //if button released
        if(!get_button_level())
        {
            //generate quick press event and exit long-press check, and skip release check
            press_check = true;
            released = true; 
            generate_quick_press_evt(); //generate quick press event
            vTaskDelay(25/portTICK_PERIOD_MS); //release debounce
        } 
        //long-press event time exceeded
        else if((esp_timer_get_time() - time_stamp) > button_conf.long_press_evt_time)
        {
            //generate long press event and exit long-press check, progress to release check
            press_check = true; 
            generate_long_press_evt(); //generate long press event
        }

        vTaskDelay(15/portTICK_PERIOD_MS);
    }

    return released;

}

void Button::released_check()
{
    bool released_check = false;
    int64_t time_stamp = esp_timer_get_time(); 

    //release check: generate held events until release event
    while(!released_check)
    {
        //if button released
        if(!get_button_level())
        {
            //generate release event and exit release check
            released_check = true;
            generate_released_evt();
            vTaskDelay(25/portTICK_PERIOD_MS); //release debounce
        }
        //if held event generation time exceeded
        else if((esp_timer_get_time() - time_stamp) > button_conf.held_evt_time)
        {
            //reset time stamp generate held event
            time_stamp = esp_timer_get_time(); 
            generate_held_evt();
                        
        }

        vTaskDelay(15/portTICK_PERIOD_MS); //delay for idle task
    }
}

bool Button::get_button_level()
{
    if(button_conf.active_hi)
        return gpio_get_level(button_conf.gpio_num);  //if active-high button return true when gpio is high
    else if(button_conf.active_lo)
        return !gpio_get_level(button_conf.gpio_num); //if active-low button return true when gpio is low
    else
        return false; //if this statement is executed, user ignored error codes and backtrace dump from constructor

}

void Button::generate_quick_press_evt()
{
    event.set(ButtonEvent::quick_press);
    pending_scan = true;
    if(logging_en)
        ESP_LOGI(TAG, "%s -> quick-press", name);
}

void Button::generate_long_press_evt()
{
    event.set(ButtonEvent::long_press);
    pending_scan = true;
    if(logging_en)
        ESP_LOGI(TAG, "%s -> long-press", name);
}

void Button::generate_held_evt()
{
    event.set(ButtonEvent::held);
    pending_scan = true;
    if(logging_en)
        ESP_LOGI(TAG, "%s -> held", name);
}

void Button::generate_released_evt()
{
    event.set(ButtonEvent::released);
    pending_scan = true;
    if(logging_en)
        ESP_LOGI(TAG, "%s -> released", name);
}


void IRAM_ATTR Button::button_handler(void *arg)
{
     BaseType_t xHighPriorityTaskWoken = pdFALSE;
    Button *button = (Button *)arg;
    gpio_intr_disable(button->button_conf.gpio_num); //disable interrupt until re-enabled in button task

    vTaskNotifyGiveFromISR(button->button_task_hdl, &xHighPriorityTaskWoken); //notify button task button input was detected
    portYIELD_FROM_ISR(xHighPriorityTaskWoken); //perform context switch if necessary
    
    
}

