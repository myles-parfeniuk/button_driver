    #pragma once
    
    /** 
    *   @brief  the different kinds of button events that can be detected
    */
    enum class ButtonEvent 
    {
      quick_press, ///<quick-press event, generated if button is pressed, then released before (25ms + button->button_conf.long_press_evt_time) elapses
      long_press, ///<long-press event, generated if button is pressed, and not released after (25ms + button->button_conf.long_press_evt_time) elapses
      held, ///<held event, generated if button is held after long-press event has been generated, re-generates every button->button_conf->hold_evt_time microseconds until button is released 
      released ///<release event, generated when button is released (note, button release event not generated for quick-press events)
    };