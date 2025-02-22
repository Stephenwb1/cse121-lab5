#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#ifndef	CONSUMER
#define	CONSUMER	"Consumer"
#endif
#define chipname "gpiochip0"
#define line_num 17
#define UNIT 200000 // 2000000 microseconds = 200ms

const char *morse_table[128] = {
    ['A'] = ".-",    ['B'] = "-...",  ['C'] = "-.-.", ['D'] = "-..",
    ['E'] = ".",     ['F'] = "..-.",  ['G'] = "--.",  ['H'] = "....",
    ['I'] = "..",    ['J'] = ".---",  ['K'] = "-.-",  ['L'] = ".-..",
    ['M'] = "--",    ['N'] = "-.",    ['O'] = "---",  ['P'] = ".--.",
    ['Q'] = "--.-",  ['R'] = ".-.",   ['S'] = "...",  ['T'] = "-",
    ['U'] = "..-",   ['V'] = "...-",  ['W'] = ".--",  ['X'] = "-..-",
    ['Y'] = "-.--",  ['Z'] = "--..",

    ['0'] = "-----", ['1'] = ".----", ['2'] = "..---", ['3'] = "...--",
    ['4'] = "....-", ['5'] = ".....", ['6'] = "-....", ['7'] = "--...",
    ['8'] = "---..", ['9'] = "----.",

    [' '] = "/"  // Word separator
};

void flash_symbol(struct gpiod_line *line, char symbol) {
    if (symbol == '.') {
        gpiod_line_set_value(line, 1);
        usleep(UNIT);
    } else if (symbol == '-') {
        gpiod_line_set_value(line, 1);
        usleep(UNIT * 3);
    }
    gpiod_line_set_value(line, 0);
    usleep(UNIT);  // space between symbols
}

void send_morse (struct gpiod_line *line, const char *message, int repeat) {
  for (int r = 0; r < repeat; r++) {
        for (size_t i = 0; i < strlen(message); i++) {
            char c = toupper(message[i]);
            const char *code = morse_table[(int)c];
            if (!code) continue;

            if (strcmp(code, "/") == 0) {
                usleep(UNIT * 7);  // space between words
                continue;
            }

            for (size_t j = 0; j < strlen(code); j++) {
                flash_symbol(line, code[j]);
            }
            usleep(UNIT * 3);  // space between letters
        }
        usleep(UNIT * 7);  // space between repeats
    }
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <repeat> \"message\"\n", argv[0]);
    return 1;
  }

  int repeat = atoi(argv[1]);
  const char *msg = argv[2];

	unsigned int val;
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	int i, ret;

	chip = gpiod_chip_open_by_name(chipname);
	if (!chip) {
		perror("Open chip failed\n");
		goto end;
	}

	line = gpiod_chip_get_line(chip, line_num);
	if (!line) {
		perror("Get line failed\n");
		goto close_chip;
	}

	ret = gpiod_line_request_output(line, CONSUMER, 0);
	if (ret < 0) {
		perror("Request line as output failed\n");
		goto release_line;
	}

	// /* Blink 20 times */
	// val = 0;
	// for (i = 20; i > 0; i--) {
	// 	ret = gpiod_line_set_value(line, val);
	// 	if (ret < 0) {
	// 		perror("Set line output failed\n");
	// 		goto release_line;
	// 	}
	// 	printf("Output %u on line #%u\n", val, line_num);
	// 	sleep(1);
	// 	val = !val;
	// }

  send_morse(line, msg, repeat);

release_line:
	gpiod_line_release(line);
close_chip:
	gpiod_chip_close(chip);
end:
	return 0;
}
