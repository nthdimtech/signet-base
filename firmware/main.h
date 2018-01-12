#ifndef MAIN_H

void led_on();
void led_off();
void start_blinking(int period, int duration);
void stop_blinking();
void pause_blinking();
void resume_blinking();
int is_blinking();

#endif
