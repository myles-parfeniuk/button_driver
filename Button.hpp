#pragma once

#include "../data_control/DataControl.hpp"

//esp-idf includes
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

//standard library includes
#include <iostream>

/**
* @brief Class representing push-button or tactile switch, has self-contained driver. 
*
* Encapsulates a physical button or tactile switch and methods to interact with it.
*
* Call the follow() method on the event member of a button to register a call-back function (see data_control documentation for more info on follow()).
* 
* Any call-back functions registered to a button's event member will be executed every time a button event is detected.   
* Call-back functions can be registered in as many places as desired using follow(), where each button event can be handled in multiple places,
* independently. 
*
* @author Myles Parfeniuk
*
*/
class Button {       
  public:             

    /** 
    *   @brief  The different kinds of button events that can be detected.
    */
    enum class ButtonEvent 
    {
      quick_press, ///<quick-press event, generated if button is pressed, then released before (25ms + button->button_conf.long_press_evt_time) elapses
      long_press, ///<long-press event, generated if button is pressed, and not released after (25ms + button->button_conf.long_press_evt_time) elapses
      held, ///<held event, generated if button is held after long-press event has been generated, re-generates every button->button_conf->hold_evt_time microseconds until button is released 
      released ///<release event, generated when button is released (note, button release event not generated for quick-press events)
    };

    struct button_config_t{
        gpio_num_t gpio_num; ///<the gpio number associated with the button
        bool active_lo; ///<true if the button is active-low (falling edge), false if otherwise
        bool active_hi; ///<true if the button is active-high (rising edge), false if otherwise
        bool pull_en; ///<true if internal pullup (for active low) or pulldown (for active high) should be enabled, false if using external pullup/pulldown
        int64_t long_press_evt_time; /**< long-press event generation time in microseconds (us), if the button is held for longer than (long_press_evt_time+25ms) 
                                          a long-press event is generated, if it is released before (long_press_evt_time+25ms) elapses a quick-press event is generated instead*/
        int64_t held_evt_time; /**< held event generation time in microseconds (us), if the button is held for longer than (25ms+long_press_evt_time+hold_evt_time),  
                                    held events will be generated every held_evt_time us*/
    }; 

  /**
  * @brief Construct a new Button object.
  * 
  * Constructs a new button object, if button_conf is not correctly initialized an error message will be printed to terminal and a backtrace will be dumped.
  * 
  * For correct initialization, ensure that:
  * 
  * - button_conf.active_lo is the opposite of button_conf.active_hi (button can only be active high or active low, not both). An active high button is one that
  * outputs a logic high when pressed, an active low button is one that outputs a logic low when pressed.
  * 
  * - button_conf.gpio_num is correctly set with the gpio number that is connected to the button
  * 
  * - button_conf.long_press_evt_time is between 10000us and 5000000us
  * 
  * - button_conf.held_evt_time is between 10000us and 5000000us
  * 
  * @param button_conf button configuration information, contains information about whether the button is active high or low, which GPIO number is connected to button, 
  *                    event generation times, and if internal pullups/pulldowns should be enabled.
  * @param logging_en logging status, true to enable logging, false to disable (optional, default false)
  * @param name name of button object, printed out with logs to aid debugging (optional, default "unamed")
  * 
  * @return void, nothing to return
  */
    Button(button_config_t button_conf, bool logging_en = false, const char *name = "unamed");
  
  /**
  * @brief Scan for button event.
  * 
  * Scan to see if a button event has ocurred. This function can be used to
  * poll a button, if it returns true, the get() method can be called to return
  * the type of event which occurred (quick-press, long-press, held, released).
  * 
  * It is not recommended to handle button events this way, scan() can only be called
  * in a single location (ie cannot be polled in two threads at once).
  *  
  * Instead of calling scan(), it is recommended call the follow() method on the event member to register a call-back function.
  * 
  * @return void, nothing to return
  */
    bool scan(); 
    
    DataControl::CallAlways<ButtonEvent> event; ///<DataWrapper containing button event, call-back function can be registered to button with follow() method (see data_control documentation) 

    TaskHandle_t button_task_hdl; ///<task handle of the button_task

  private:
  
    button_config_t button_conf; ///<the button configuration settings 
    bool logging_en; ///<true to enable debugging logs, false otherwise
    bool pending_scan;  ///< true if an event has been asserted since the last time scan() was called
    const char *name; ///<name of button, used in debugging logs
  
  /**
  * @brief Get level of a button, called from button task
  * 
  * Get the current level of a button (whether the button is currently being pressed or is inactive).
  * 
  * @return true if button is currently being pressed, false if button inactive
  */
  bool get_button_level(); 
  
  /**
  * @brief Generate a quick-press event.
  * 
  * Generates a quick-press event by calling set() method on this->event. 
  * This will execute any call-backs registered to the button and set pending_scan to true.
  * 
  * @return void, nothing to return
  */
    void generate_quick_press_evt();

  /**
  * @brief Generate a long-press event.
  * 
  * Generates a long press event by calling set() method on this->event. 
  * This will execute any call-backs registered to the button and set pending_scan to true.
  * 
  * @return void, nothing to return
  */
   void generate_long_press_evt();

  /**
  * @brief Generate a held event.
  * 
  * Generates a held event by calling set() method on this->event. 
  * This will execute any call-backs registered to the button and set pending_scan to true.
  * 
  * @return void, nothing to return
  */
   void generate_held_evt();

  /**
  * @brief Generate a released event.
  * 
  * Generates a released event by calling set() method on this->event. 
  * This will execute any call-backs registered to the button and set pending_scan to true.
  * 
  * @return void, nothing to return
  */
   void generate_released_evt();
   
  /**
  * @brief Detect quick-press or long-press event.
  * 
  * Generates a quick-press event if button is released before (25ms + button->button_conf.long_press_evt_time) elapse.
  * Generates a long-press event otherwise. 
  * 
  * @return True if button was released (quick-press event generated), false if button was held for until long_press_evt_time elapsed. 
  */
    bool press_check();
  
  /**
  * @brief Generate held events until button is released.
  * 
  * Generates held events every button->button_conf.held_evt_time microseconds.
  * If the button is released a release event will be generated and the function will exit.
  * 
  * @return void, nothing to return
  */
  void released_check();
  
  /**
  * @brief Task responsible for determining and generating events.
  * 
  * This task is notified by the button_handler ISR when any button activity is detected. 
  * It determines the correct button events and generates them until the button is released.
  * After the button is released the task waits until it is again notified by the button_handler ISR. 
  * 
  * button activity triggers isr --> interrupt disabled --> button_task notified --> events are determined and generated --> 
  * button release detected --> interrupt enabled --> button_task waits for notification
  * 
  * @return void, nothing to return
  */
  void button_task();


  /**
  * @brief Launches button task.
  * 
  * This function is used to get around the fact xTaskCreate() from the freertos api requires a static task function.
  * 
  * To prevent having to write the button task from the context of a static function, this launches the button_task()
  * from the Button object passed into xTaskCreate().
  * 
  * @param arg a pointer to the button to detect events for casted to a void pointer
  * @return void, nothing to return
  */
  static void button_task_trampoline(void *arg);

 
  /**
  * @brief ISR handler responsible for handling button activity.
  * 
  * This ISR is entered when a falling edge is detected (for an active low button) or a rising edge (for an active high button).
  * It resumes the button_task and disables the button's interrupt. 
  * 
  * Button interrupt is re-enabled in button_task upon button release. 
  * 
  * button activity triggers isr --> interrupt disabled --> button_task notified --> events are determined and generated --> 
  * button release detected --> interrupt enabled --> button_task waits for notification
  * 
  * @return void, nothing to return
  */
    static void IRAM_ATTR button_handler(void *arg);

    static const constexpr char* TAG = "Button"; ///<class tag, used in debug logs
    static bool isr_service_installed; ///<true of the isr service has been installed, false until first button is created
 
   
};