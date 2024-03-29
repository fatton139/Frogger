/*
 * FroggerProject.c
 *
 * Main file
 *
 * Author: Peter Sutton. Modified by Xinyi Li
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <math.h>

#include "project.h"
#include "ledmatrix.h"
#include "scrolling_char_display.h"
#include "buttons.h"
#include "serialio.h"
#include "terminalio.h"
#include "score.h"
#include "timer0.h"
#include "game.h"
#include "countdown.h"
#include "joystick.h"
#include "sound.h"
#include "eeprom.h"

#define F_CPU 8000000L
#include <util/delay.h>

// Function prototypes - these are defined below (after main()) in the order
// given here
void initialise_hardware(void);
void splash_screen(void);
void new_game(void);
void play_game(void);
void handle_game_over(void);
void init_life(void);
void set_life(uint8_t life);

// ASCII code for Escape character
#define ESCAPE_CHAR 27

uint8_t seven_seg[10] = {63,6,91,79,102,109,125,7,127,111};

/////////////////////////////// main //////////////////////////////////
int main(void) {
	// Setup hardware and call backs. This will turn on 
	// interrupts.
	initialise_hardware();
	// Show the splash screen message. Returns when display
	// is complete
	splash_screen();
	while(1) {
		new_game();
		play_game();
		handle_game_over();
	}
}

void initialise_hardware(void) {
	ledmatrix_setup();
	init_button_interrupts();

	// Setup serial port for 19200 baud communication with no echo
	// of incoming characters
	init_serial_stdio(19200,0);
	
	// Initialise joystick
	initialise_joystick();
	
	// Setup timer
	init_timer0();
	
	// Setup hardware to track lives.
	init_life();

	// Setup count down timer
	init_countdown();
	
	// Setup sounds
	init_sound();

	// Turn on global interrupts
	sei();
}

void splash_screen(void) {
	// Clear terminal screen and output a message
	clear_terminal();
	move_cursor(10,10);
	printf_P(PSTR("Frogger"));
	move_cursor(10,12);
	printf_P(PSTR("CSSE2010/7201 project by Xinyi Li"));
	read_eeprom();
	move_cursor(10,20);
	// Output the scrolling message to the LED matrix
	// and wait for a push button to be pushed.
	ledmatrix_clear();
	while(1) {
		set_scrolling_display_text("FROGGER S4478355", COLOUR_GREEN);
		// Scroll the message until it has scrolled off the 
		// display or a button is pushed
		while(scroll_display()) {
			_delay_ms(150);
			if(button_pushed() != NO_BUTTON_PUSHED) {
				return;
			}
		}
	}
}

uint8_t* request_name(void) {
	clear_terminal();
	read_eeprom();
	move_cursor(0, 0);
	printf("\nYou achieved a new highscore!\n");
	printf("Your name: ");
	static uint8_t name[12];
	int i = 0;
	//int cursor_position = 12;
	while (1) {
		if(serial_input_available()) {
			char serial_input = fgetc(stdin);
			if (serial_input == '\n') {
				break;
			} else if (serial_input == 127) {
			} else if (serial_input == 68) {
				move_left();
				i--;
			} else {
				if (i < 10) {
					printf("%c", serial_input);
					name[i] = serial_input;
					i++;
				}
			}
		}
	}
	name[12] = '\0';
	clear_terminal();
	return name;
}

// Set up life tracker with PORT D0 - D4.
void init_life(void) {
	current_life = STARTING_LIVES;
	DDRA |= 0b00011111;
}

// Sets the current life of a player.
void set_life(uint8_t life) {
	uint8_t new_life = 0;
	for (int i = 0; i < life; i++) {
		new_life += i >= 2 ? pow(2, i) + 1 : pow(2, i);
	}
	PORTA &= ~(1<<4) | ~(1<<3) | ~(1<<2) | ~(1<<1) | ~(1<<0);
	PORTA = new_life;
}

void print_stats() {
	move_cursor(10,1);
	printf("\nYour score is: %9lu\n", get_score());
	move_cursor(10,2);
	printf("\nCurrent Level: %9i \n", current_level + 1);
	move_cursor(10,3);
	printf("\nLives remaining: %7i\n", current_life);
}

void new_game(void) {

	// Initialise the game and display
	initialise_game();
	
	// Clear the serial terminal
	clear_terminal();
	
	// Reset the countdown timer
	reset_countdown();
	
	// Enable Joystick
	joystick_enable = 1;
	if (!on_same_game) {
		// If all lives are expended, reset the lives to start a fresh game.
		current_level = 0;
		current_life = STARTING_LIVES;
		set_life(current_life);
		// Initialise the score
		init_score();
	} else {
		move_cursor(10,1);
		printf("\nYour score is: %9lu\n", get_score());
	}
	// Clear a button push or serial input if any are waiting
	// (The cast to void means the return value is ignored.)
	(void)button_pushed();
	clear_serial_input_buffer();
}

void play_game(void) {
	uint32_t current_time, last_move_time, last_button_down, last_joy_held, sound_play_time;
	uint8_t button; 
	uint8_t pressed_button = NO_BUTTON_PUSHED;
	char serial_input, escape_sequence_char;
	uint8_t characters_into_escape_sequence = 0;
	int count_ms = 0;
	uint8_t tone_at = 0;
	// Get the current time and remember this as the last time the vehicles
	// and logs were moved.
	current_time = get_current_time();
	last_move_time = current_time;
	sound_play_time = current_time;
	
	// Get the current time and remember the last time the button was pushed.
	last_button_down = current_time + 500;
	
	// We play the game while the frog is alive and we haven't filled up the 
	// far riverbank
	
	//Joystick control variables
	last_joy_held = current_time + 500;
	uint8_t x_or_y = 0;
	int x = 500;
	int y = 500;
	int last_direction = 0;
	int joy_held = 0;
	int is_first_pass = 1;
	
	// Setup array of counters;
	int counters[7] = {0, 0, 0, 0, 0, 0, 0};
	while(!is_frog_dead() && !is_riverbank_full()) {
		
		// Display the time remaining
		if (time_remaining_s >= 10) {
			if (cc)
				display_digit(seven_seg[1], 1, 0);
			else
				display_digit(seven_seg[time_remaining_s % 10], 0, 0);
		} else if (time_remaining_s > 1)
			display_digit(seven_seg[time_remaining_s % 10], 0, 0);
		else {
			count_ms = 1;
			if (time_remaining_ms > 10) {
					display_digit(seven_seg[1], 0, 0);
			} else {
				if (cc)
					display_digit(seven_seg[0], 1, 1);
				else
					display_digit(seven_seg[time_remaining_ms > 0 ? time_remaining_ms - 1 : 0], 0, 0);
				
			}
		}
		
		// Game over if timer has reached 0
		if (time_remaining_ms == 0) {
			display_digit(seven_seg[0], 0, 0);
			count_ms = 0;
			frog_dead = 1;
			redraw_frog();
			break;
		}

		if(!is_frog_dead() && frog_has_reached_riverbank()) {
			// Frog reached the other side successfully but the
			// riverbank isn't full, put a new frog at the start
			put_frog_in_start_position();
		}
		
		// Check for input - which could be a button push or serial input.
		// Serial input may be part of an escape sequence, e.g. ESC [ D
		// is a left cursor key press. At most one of the following three
		// variables will be set to a value other than -1 if input is available.
		// (We don't initalise button to -1 since button_pushed() will return -1
		// if no button pushes are waiting to be returned.)
		// Button pushes take priority over serial input. If there are both then
		// we'll retrieve the serial input the next time through this loop
		serial_input = -1;
		escape_sequence_char = -1;
		button = button_pushed();

		if(button == 255) {
			// No push button was pushed, see if there is any serial input
			if(serial_input_available()) {
				// Serial data was available - read the data from standard input
				serial_input = fgetc(stdin);
				// Check if the character is part of an escape sequence
				if(characters_into_escape_sequence == 0 && serial_input == ESCAPE_CHAR) {
					// We've hit the first character in an escape sequence (escape)
					characters_into_escape_sequence++;
					serial_input = -1; // Don't further process this character
				} else if(characters_into_escape_sequence == 1 && serial_input == '[') {
					// We've hit the second character in an escape sequence
					characters_into_escape_sequence++;
					serial_input = -1; // Don't further process this character
				} else if(characters_into_escape_sequence == 2) {
					// Third (and last) character in the escape sequence
					escape_sequence_char = serial_input;
					serial_input = -1;  // Don't further process this character - we
										// deal with it as part of the escape sequence
					characters_into_escape_sequence = 0;
				} else {
					// Character was not part of an escape sequence (or we received
					// an invalid second character in the sequence). We'll process 
					// the data in the serial_input variable.
					characters_into_escape_sequence = 0;
				}
			}
		}
		
		if(x_or_y == 0) {
			ADMUX = (1<<REFS0) | (1<<MUX2) | (1<<MUX0);
			y = ADC;
		} else {
			ADMUX = (1<<REFS0) | (1<<MUX2) | (1<<MUX1);
			x = ADC;
		}
		ADCSRA |= (1<<ADSC);
		
		// On the first cycle of the loop the joy stick is in a unusual configuration.
		if (is_first_pass) {
			x = 500;
			y = 500;
			play_sound(500, 200);
		}
		
		uint32_t mag = sqrt(pow(x - 500, 2) + pow(y - 500, 2));
		int angle = atan2(y - 500, x - 500) * (180/M_PI);
		
		if (mag > 400) {
			if (last_direction != 1 && angle > 60 && angle < 120) {
				move_frog_forward();
				last_direction = 1;
			} else if (last_direction != 2 && angle < -60 && angle > -120) {
				move_frog_backward();
				last_direction = 2;
			} else if (last_direction != 3 && angle > -30 && angle < 30) {
				move_frog_to_left();
				last_direction = 3;
			} else if (last_direction != 4 && (angle < -150 || angle > 150)) {
				move_frog_to_right();
				last_direction = 4;
			} else if (last_direction != 5 && angle > 30 && angle < 60) {
				move_frog_up_left();
				last_direction = 5;
			} else if (last_direction != 6 && angle > 120 && angle < 150) {
				move_frog_up_right();
				last_direction = 6;
			} else if (last_direction != 7 && angle < -30 && angle > -60) {
				move_frog_down_left();
				last_direction = 7;
			} else if (last_direction != 8 && angle < -120 && angle > -150) {
				move_frog_down_right();
				last_direction = 8;
			}
			if (!joy_held && last_direction != 0) {
				joy_held = 1;
			}
		} else {
			last_direction = 0;
			joy_held = 0;
		}
		
		if (!joy_held) {
			last_joy_held = current_time + 500;
		}
		
		if (current_time >= last_joy_held && joy_held) {
			last_joy_held = current_time + 100;
			switch (last_direction) {
				case 1:
					move_frog_forward();
					break;
				case 2:
					move_frog_backward();
					break;
				case 3:
					move_frog_to_left();
					break;
				case 4:
					move_frog_to_right();
					break;
				case 5:
					move_frog_up_left();
					break;
				case 6:
					move_frog_up_right();
					break;
				case 7:
					move_frog_down_left();
					break;
				case 8:
					move_frog_down_right();
					break;
			}
		}
		
		x_or_y = !x_or_y;
		
		if (is_first_pass)
			is_first_pass = 0;
		
		// Add a delay to the hold before triggering auto delay.
		if (!button_down) {
			last_button_down = current_time + 500;
		}

		// Auto repeat when a button is held down.
		if (current_time >= last_button_down && button_down) {
			last_button_down = current_time + 100;
			// Account for unusual intervals which causes the button to be a unexpected value.
			if (pressed_button <= 3) {
				switch (pressed_button)
				{
					case 3:
						move_frog_to_left();
						break;
					case 2:
						move_frog_forward();
						break;
					case 1:
						move_frog_backward();
						break;
					case 0:
						move_frog_to_right();
						break;
				}
			}
		}
		
		// Process the input. 
		if(button==3 || escape_sequence_char=='D' || serial_input=='L' || serial_input=='l') {
			// Attempt to move left
			// Remember the button pressed.
			pressed_button = button;
			move_frog_to_left();
			
		} else if(button==2 || escape_sequence_char=='A' || serial_input=='U' || serial_input=='u') {
			// Attempt to move forward
			// Remember the button pressed
			pressed_button = button;
			move_frog_forward();
			
		} else if(button==1 || escape_sequence_char=='B' || serial_input=='D' || serial_input=='d') {
			// Attempt to move down
			// Remember the button pressed.
			pressed_button = button;
			move_frog_backward();
			
		} else if(button==0 || escape_sequence_char=='C' || serial_input=='R' || serial_input=='r') {
			// Attempt to move right
			// Remember the button pressed.
			pressed_button = button;
			move_frog_to_right();
			
		} else if(serial_input == 'p' || serial_input == 'P') {
			paused = !paused;
		} 
		// else - invalid input or we're part way through an escape sequence -
		// do nothing
		
		// Reset the pressed button if no button is being held.
		if (!button_down) {
			pressed_button = NO_BUTTON_PUSHED;
		}
		
		current_time = get_current_time();
		
		if (tone_at < 2) {
			if (current_time >= sound_play_time + 200) {
				play_sound(800, 300);
				tone_at = 1;
			}
			
			if (current_time >= sound_play_time + 500) {
				play_sound(1500, 500);
				tone_at = 2;
			}
		}
		
		// Reduce the cycle times times	
		if(!is_frog_dead() && current_time >= last_move_time + 100) {
			// Since the counters tick up every 100ms we can effectively set custom cycle times
			// by adjusting the max value the counter should tick up to.
			// 1000ms (10 * 100) cycle
			double scale = current_level < 6 ? current_level : current_level * (1.1);
			if (!paused) {
				if (counters[0] > (10 - scale)) {
					scroll_vehicle_lane(0, 1);
					counters[0] = 0;
				}
				// 1300ms (13 * 100) cycle
				if (counters[1] > (13 - scale)) {
					scroll_vehicle_lane(1, -1);
					counters[1] = 0;
				}
				// 800ms (8 * 100) cycle
				if (counters[2] > (8 - scale)) {
					scroll_vehicle_lane(2, 1);
					counters[2] = 0;
				}
				// 900ms (9 * 100) cycle
				if (counters[3] > (9 - scale)) {
					scroll_river_channel(0, -1);
					counters[3] = 0;
				}
				// 1000ms (10 * 100) cycle
				if (counters[4] > (11 - scale)) {
					scroll_river_channel(1, 1);
					counters[4] = 0;
				}
				// Count down the timer in seconds
				if (counters[5] > 10) {
					time_remaining_s--;
					counters[5] = 0;
				}
				// Count down the timer in ms
				if (counters[5] > 1 && count_ms) {
					time_remaining_ms--;
					counters[6] = 0;
				}
				// Increment each counter every cycle.
				for (int i = 0; i < (sizeof(counters) / sizeof(int)); i++) {
					counters[i]++;
				}
				last_move_time = current_time;
			}
		}
	}
	

	// We get here if the frog is dead or the riverbank is full
	// The game is over.
}

void handle_game_over() {
	// Reduce lives until it reaches 0 before proceeding with the normal procedure of
	// game over handle.
	on_same_game = 1;
	play_sound(1000, 1000);
	if (is_riverbank_full()) {
		display_digit(seven_seg[(current_level % 10) + 1], 1, 0);
		move_cursor(10,14);
		printf("\n Current Level: %i \n", current_level);
		set_scrolling_display_text("", 0);
		int i = 0;
		while(scroll_display() && i < 15) {
			_delay_ms(100);
			i++;
		}
		_delay_ms(100);
		current_level++;
		if (current_life < 5)
			set_life(++current_life);
	} else {
		display_digit(seven_seg[0], 1, 0);
		set_life(--current_life);
		if (current_life <= 0) {
			on_same_game = 0;
			move_cursor(10,5);
			if (get_score() > 0)
				compare_and_update(get_score());
			read_eeprom();
			move_cursor(10,14);
			printf_P(PSTR("GAME OVER"));
			move_cursor(10,15);
			printf_P(PSTR("Press a button to start again"));
		}
		joystick_enable = 0;
		print_stats();
		while(button_pushed() == NO_BUTTON_PUSHED) {
			; // wait
		}
	}
}