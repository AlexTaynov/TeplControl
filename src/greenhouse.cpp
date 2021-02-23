#include <GyverTimer.h>
#include <GyverButton.h>
#include <OneWire.h>
#include <avr/eeprom.h>
#include <LCD_1602_RUS.h>

#define BCKLIGHT_TIMEOUT 20       //sec
#define QUIET_MODE_TIMEOUT 50     //minutes
#define DS18_POLLING_TIMEOUT 2000 //ms
#define CLEAR_LCD_IN_MS 200       //ms

#define INACTION_TIMEOUT 30
#define INCREMENTING_TIME_FOR_QUIET_TIMER 10 //minutes
#define DECREMENTING_TIME_FOR_QUIET_TIMER 10 //minutes
#define DELTA 1                              // температурная дельта, градусов

#define pin_DS18 2
#define pin_buzzer 3
#define pin_relay 4
#define pin_led_relay 7
#define pin_voltage_sensor 8
#define pin_ok_button 9
#define pin_inc_button 10
#define pin_dec_button 11

int params[] = {1, 4, 7, 39, 1, 0, 1, 1, BCKLIGHT_TIMEOUT, QUIET_MODE_TIMEOUT};

OneWire ds18(pin_DS18);
LCD_1602_RUS lcd(0x27, 16, 2);

GButton inc_button(pin_inc_button);
GButton dec_button(pin_dec_button);
GButton ok_button(pin_ok_button);
GTimer backlight_timer(MS);
GTimer quiet_mode_timer(MS);
GTimer ds18_polling_timer(MS);
GTimer clear_lcd(MS);
GTimer return_timer(MS);

int current_temperature = 0;
String rest_time_quiet = "99:99";
byte alarm_on = 0;

void backlight_control()
{
    backlight_timer.isReady();
    ((backlight_timer.isEnabled()) ? lcd.backlight() : lcd.noBacklight());
}

void init_pins()
{
    pinMode(pin_buzzer, OUTPUT);
    pinMode(pin_relay, OUTPUT);
    pinMode(pin_voltage_sensor, INPUT);
    pinMode(pin_led_relay, OUTPUT);
}
GTimer ds18b20_timer;

void get_temperature()
{
    int result = INT16_MAX;
    byte data[2];
    ds18.reset();
    ds18.write(0xCC);
    ds18.write(0x44);
    if (ds18b20_timer.isReady())
    {
        ds18.reset();
        ds18.write(0xCC);
        ds18.write(0xBE);
        data[0] = ds18.read();
        data[1] = ds18.read();
        result = (data[1] << 8) + data[0];
        result >>= 4;
        current_temperature = result;
    }
}

GTimer melody_timer;
void melody()
{
    if (melody_timer.isReady())
    {
        tone(pin_buzzer, 440, 100);
    }
}

byte alarm_mode = 0;
void do_alarm()
{

    if (ok_button.isHolded())
    {
        alarm_mode ^= 1;
        (!alarm_mode ? quiet_mode_timer.setTimeout(params[9] * 60000) : quiet_mode_timer.stop());
    }

    switch (alarm_mode)
    {
    case 0:
    {

        if (quiet_mode_timer.isReady())
        {
            alarm_mode = 1;
            rest_time_quiet = "";
            quiet_mode_timer.stop();
        }
        else
        {
            unsigned long ot = quiet_mode_timer.restTime();
            byte hours = (int)(ot / 1000UL / 3600UL);
            byte minutes = (int)(ot / 60000UL) % 60;
            rest_time_quiet = (hours < 10 ? "0" : "") + (String)hours + ":" + (minutes < 10 ? "0" : "") + (String)minutes;
        }

        break;
    }
    case 1:
    {
        melody();
        break;
    }
    }
} // end do_alarm()

void process_temperature()
{

    if (params[6])
    { //alarm
        if (current_temperature <= params[0])
        {
            alarm_on |= 1;
        }
        else if (current_temperature >= params[0] + DELTA)
        {
            alarm_on &= ~1;
        }

        if (current_temperature <= params[3] - DELTA)
        {
            alarm_on &= ~(1 << 1);
        }
        else if (current_temperature >= params[3])
        {
            alarm_on |= 1 << 1;
        }
    }
    else
    {
        alarm_on &= ~1;
        alarm_on &= ~(1 << 1);
    }

    if (!params[4])
    {
        if (current_temperature <= params[1])
        {
            digitalWrite(pin_relay, HIGH);
            digitalWrite(pin_led_relay, LOW);
        }
        else if (current_temperature >= params[2])
        {
            digitalWrite(pin_relay, LOW);
            digitalWrite(pin_led_relay, HIGH);
        }
    }
    else
    {
        digitalWrite(pin_relay, params[5]);
        digitalWrite(pin_led_relay, !params[5]);
    }

} // end process_temperature()

void check_voltage()
{
    if (params[7])
    {
        static byte checking_voltage = 0;
        checking_voltage = (digitalRead(pin_voltage_sensor) ? constrain(checking_voltage + 1, 0, 4) : 0);
        if (checking_voltage == 4)
            alarm_on |= 1 << 2;
        else
            alarm_on &= ~(1 << 2);
    }
    else
    {
        alarm_on &= ~(1 << 2);
    }
}

void init_params()
{
    eeprom_read_block((void *)&params, (void *)5, sizeof(params));
}

void write_params_to_eeprom()
{
    eeprom_write_block((void *)&params, (void *)5, sizeof(params));
}

void update_params()
{
    static int prev_params[10];
    bool changed = 0;
    for (int i = 0; i < 10; i++)
    {
        if (prev_params[i] != params[i])
        {
            changed = 1;
            prev_params[i] = params[i];
        }
    }
    if (changed)
        write_params_to_eeprom();
}

void main_screen()
{
    lcd.setCursor(0, 0);
    lcd.print(F("Temp: "));
    lcd.print(current_temperature);
    static int prev_temp = 0;
    if (current_temperature != prev_temp)
    {
        prev_temp = current_temperature;
        lcd.clear();
    }

    if (alarm_on)
    {
        lcd.setCursor(9, 0);
        lcd.print(F("Trevoga"));

        if (alarm_mode == 0)
        {
            lcd.setCursor(0, 1);
            lcd.print(F("Tixiy"));
            lcd.setCursor(11, 1);
            lcd.print(rest_time_quiet);
        }
    }
}

void print_pointer(int8_t cursor, bool isChoosen)
{
    lcd.setCursor(0, cursor % 2);
    lcd.print(!isChoosen ? F(">") : F("~"));
}

void enable_backlight()
{
    if (ok_button.isPress() || dec_button.isPress() || inc_button.isPress())
    {
        backlight_timer.start();
        lcd.clear();
    }
}

void menu_relay()
{
    byte pointer = 0;
    bool is_choosed = false;
    while (true)
    {
         if(return_timer.isReady()) {
            return_timer.start();
            return;
        }
        ok_button.tick();
        inc_button.tick();
        dec_button.tick();

        enable_backlight();

        if (!backlight_timer.isEnabled() && (ok_button.isClick() || dec_button.isClick() || inc_button.isClick()))
        {
            return;
        }

        if (ok_button.isClick())
        {
            lcd.clear();
            if (pointer != 0)
                is_choosed = !is_choosed;
            else
                return;
        }

        if (inc_button.isClick() || inc_button.isHold())
        {
            lcd.clear();
            if (is_choosed && pointer != 0)
                params[pointer + 3] ^= 1;
            else
            {
                if (pointer - 1 < 0)
                    pointer += 3;
                pointer = (pointer - 1) % (3 - !params[4]);
            }
        }
        if (dec_button.isClick() || dec_button.isHold())
        {

            lcd.clear();
            if (is_choosed && pointer != 0)
                params[pointer + 3] ^= 1;
            else
                pointer = (pointer + 1) % (3 - !params[4]);
        }

        if (pointer < 2)
        {
            lcd.setCursor(1, 0);
            lcd.print(F("Hazad"));
            lcd.setCursor(1, 1);
            lcd.print((params[4] ? F("py4Hoe") : F("by tеmpеr.")));
        }
        else
        {
            if (params[4])
            {
                lcd.setCursor(1, 0);
                lcd.print((params[5] ? F("Relay on") : F("Relay off")));
            }
        }
        print_pointer(pointer, is_choosed);
    }
}

void menu_temp()
{
    byte pointer = 0;
    bool is_choosed = false;
    while (true)
    {
         if(return_timer.isReady()) {
            return_timer.start();
            return;
        }
        ok_button.tick();
        inc_button.tick();
        dec_button.tick();

        enable_backlight();
        if (pointer == 0 && ok_button.isClick())
        {
            lcd.clear();
            return;
        }
        if (ok_button.isClick())
        {
            is_choosed = !is_choosed;
            lcd.clear();
        }

        if (inc_button.isClick() || inc_button.isHold())
        {
            lcd.clear();
            if (is_choosed && pointer != 0)
                params[pointer - 1] += 1;
            else
            {
                if (pointer - 1 < 0)
                    pointer += 5;
                pointer = (pointer - 1) % 5;
            }
        }
        if (dec_button.isClick() || dec_button.isHold())
        {
            lcd.clear();
            if (is_choosed && pointer != 0)
                params[pointer - 1] -= 1;
            else
                pointer = (pointer + 1) % 5;
        }


        if (pointer < 2)
        {
            lcd.setCursor(1, 0);
            lcd.print(F("Hazad"));
            lcd.setCursor(1, 1);
            lcd.print(F("MinAlrmTemp "));
            lcd.print(params[0]);
        }
        else if (pointer < 4)
        {
            lcd.setCursor(1, 0);
            lcd.print(F("RelayOnTemp "));
            lcd.print(params[1]);
            lcd.setCursor(1, 1);
            lcd.print(F("PompOffTemp "));
            lcd.print(params[2]);
        }
        else
        {
            lcd.setCursor(1, 0);
            lcd.print(F("MaxAlrmTemp "));
            lcd.print(params[3]);
        }
        print_pointer(pointer, is_choosed);
    }
}
void menu_alarm()
{
    byte pointer = 0;
    bool is_choosed = false;
    while (true)
    {
         if(return_timer.isReady()) {
            return_timer.start();
            return;
        }

        ok_button.tick();
        inc_button.tick();
        dec_button.tick();
        enable_backlight();
        if (pointer == 0 && ok_button.isClick())
        {
            lcd.clear();
            return;
        }
        if (ok_button.isClick())
        {
            is_choosed = !is_choosed;
            lcd.clear();
        }

        if (inc_button.isClick() || inc_button.isHold())
        {
            lcd.clear();
            if (is_choosed && pointer != 0)
                params[pointer + 5] ^= 1;
            else
            {
                if (pointer - 1 < 0)
                    pointer += 3;
                pointer = (pointer - 1) % 3;
            }
        }
        if (dec_button.isClick() || dec_button.isHold())
        {
            lcd.clear();
            if (is_choosed && pointer != 0)
                params[pointer + 5] ^= 1;
            else
                pointer = (pointer + 1) % 3;
        }

        // lcd.clear();

        if (pointer < 2)
        {
            lcd.setCursor(1, 0);
            lcd.print(F("Hazad"));
            lcd.setCursor(1, 1);
            lcd.print(F("By temp. "));
            lcd.print((params[6] ? F("YES") : F("NO")));
        }
        else
        {
            lcd.setCursor(1, 0);
            lcd.print(F("By voltage "));
            lcd.print((params[7] ? F("YES") : F("NO")));
        }
        print_pointer(pointer, is_choosed);
    }
}
void menu_timer()
{
    byte pointer = 0;
    bool is_choosed = false;
    while (true)
    {
        if(return_timer.isReady()) {
            return_timer.start();
            return;
        }
        ok_button.tick();
        inc_button.tick();
        dec_button.tick();
        enable_backlight();
        if (pointer == 0 && ok_button.isClick())
        {
            lcd.clear();
            return;
        }
        if (ok_button.isClick())
        {
            is_choosed = !is_choosed;
            lcd.clear();
        }

        if (inc_button.isClick() || inc_button.isHold())
        {
            lcd.clear();

            if (is_choosed && pointer != 0)
                params[pointer + 7] += 1;
            else
            {
                if (pointer - 1 < 0)
                    pointer += 3;
                pointer = (pointer - 1) % 3;
            }
        }
        if (dec_button.isClick() || dec_button.isHold())
        {
            lcd.clear();

            if (is_choosed && pointer != 0)
                params[pointer + 7] -= 1;
            else
                pointer = (pointer + 1) % 3;
        }

        // lcd.clear();

        if (pointer < 2)
        {
            lcd.setCursor(1, 0);
            lcd.print(F("Hazad"));
            lcd.setCursor(1, 1);
            lcd.print(F("Backlgt, s "));
            lcd.print(params[8]);
        }
        else
        {
            lcd.setCursor(1, 0);
            lcd.print(F("No sound, m "));
            lcd.print(params[9]);
        }
        print_pointer(pointer, is_choosed);
    }
}

void menu_lcd()
{

    ok_button.tick();
    inc_button.tick();
    dec_button.tick();

    static byte pointer = 0;
    static byte prev_pointer = 0;

    if(prev_pointer != pointer && pointer != 0){
        return_timer.setTimeout(INACTION_TIMEOUT * 1000);
        prev_pointer = pointer;
    }
    if(return_timer.isReady() && pointer != 0){
        prev_pointer = 0;
        pointer = 0;
    }

    enable_backlight();

    if (inc_button.isClick())
    {

        if (!backlight_timer.isEnabled())
        {
            backlight_timer.start();
            return;
        }

        backlight_timer.start();

        if (pointer - 1 < 0)
            pointer += 5;
        pointer = (pointer - 1) % 5;
        lcd.clear();
    }

    if (dec_button.isClick())
    {

        if (!backlight_timer.isEnabled())
        {
            backlight_timer.start();
            return;
        }
        backlight_timer.start();

        pointer = (pointer + 1) % 5;
        lcd.clear();
    }

    if (ok_button.isClick())
    {

        if (!backlight_timer.isEnabled())
        {
            backlight_timer.start();
            return;
        }
        lcd.clear();
        backlight_timer.start();
        switch (pointer)
        {
        case 1:
            menu_relay();
            break;
        case 2:
            menu_temp();
            break;
        case 3:
            menu_alarm();
            break;
        case 4:
            menu_timer();
            break;

        default:
            break;
        }
    }

    switch (pointer)
    {
    case 0:
        main_screen();
        break;
    case 1:
    {
        lcd.setCursor(3, 0);
        lcd.print(F("Settunings"));
        lcd.setCursor(1, 1);
        lcd.print(F("Relay"));
        break;
    }
    case 2:
    {
        lcd.setCursor(3, 0);
        lcd.print(F("Settunings"));
        lcd.setCursor(1, 1);
        lcd.print(F("Termostat"));
        break;
    }
    case 3:
    {
        lcd.setCursor(3, 0);
        lcd.print(F("Settunings"));
        lcd.setCursor(1, 1);
        lcd.print(F("Sound alarm"));
        break;
    }
    case 4:
    {
        lcd.setCursor(3, 0);
        lcd.print(F("Settunings"));
        lcd.setCursor(1, 1);
        lcd.print(F("Timeout"));
        break;
    }
    }
}

void setup()
{
    init_pins();
    init_params();
    lcd.init();

    ds18b20_timer.setInterval(750);
    backlight_timer.setTimeout(params[8] * 1000);
    ds18_polling_timer.setInterval(DS18_POLLING_TIMEOUT);
    clear_lcd.setInterval(CLEAR_LCD_IN_MS);
    melody_timer.setInterval(200);
    get_temperature();
    // Serial.begin(9600);
}

void loop()
{
    for (;;)
    {

        if (ds18_polling_timer.isReady())
        {
            get_temperature();
            check_voltage();
        }

        static byte prev_alarm_on = 0;
        if (prev_alarm_on != alarm_on)
        {
            prev_alarm_on = alarm_on;
            alarm_mode = 1;
            lcd.clear();
        }

        static String prev_rest_time = "";
        if (prev_rest_time != rest_time_quiet)
        {
            prev_rest_time = rest_time_quiet;
            lcd.clear();
        }

        if (alarm_on)
        {
            do_alarm();
        }

        backlight_control();
        menu_lcd();
        process_temperature();
        update_params();
    }
}