#include "editor.h"
#include "term.h"
#include "utils.h"

#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>

static HEState *hestate = NULL;
// Instance as I
#define I hestate

#define LEN_OFFSET 9
#define PADDING 1
#define MAX_COMMAND 32

// Opens the file and stores the content
void editor_open_file(char *filename){
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL){

    }

    struct stat statbuf;
    if(stat(filename, &statbuf) == -1){
        perror("Cannot stat file");
        exit(1);
    }

    I->file_content = malloc(statbuf.st_size);
    I->file_name = malloc(strlen(filename));
    I->content_length = statbuf.st_size;

    strcpy(I->file_name, filename);

    if(fread(I->file_content, 1, statbuf.st_size, fp) < (size_t) statbuf.st_size){
        perror("Unable to read file content");
        free(I->file_content);
        exit(1);
    }
}


void editor_render_ascii(HEBuff* buff, int row, char *asc, unsigned int len){
    char c;
    for(int i = 0; i < len; i++){
        c = asc[i];

        // Cursor
        if(I->cursor_y == row){
            if(I->cursor_x == i){
                buff_append(buff, "\x1b[7m", 4);
            }
        }

        if(isprint(c)){
            buff_vappendf(buff, "%c", c);
        }else{
            buff_append(buff, ".", 1);
        }

        buff_append(buff, "\x1b[0m", 4);
    }
}

void editor_calculate_bytes_per_line(){

    // Calculate the maximum hex+ascii bytes that fits in the screen size
    // one byte = 2 chars
    int bytes_per_line = I->bytes_group; // Atleast write one byte group
    int filled;

    while((bytes_per_line / I->bytes_group) < I->groups_per_line)
    {
        bytes_per_line += I->bytes_group;
        filled = 10 + bytes_per_line * 2
            + (bytes_per_line/I->bytes_group) * PADDING + bytes_per_line;
        if (filled >= I->screen_cols){
            bytes_per_line -= I->bytes_group;
            break;
        }

    }

    I->bytes_per_line = bytes_per_line;
}

unsigned int editor_offset_at_cursor(){
    // cursor_y goes from 1 to ..., cursor_x goes from 0 to bytes_per_line
    unsigned int offset = (I->cursor_y - 1) * (I->bytes_per_line) + (I->cursor_x);
    if (offset <= 0) {
        return 0;
    }
    if (offset >= I->content_length) {
        return I->content_length - 1;
    }
    return offset;
}

void editor_cursor_at_offset(unsigned int offset){
    // cursor_y goes from 1 to ..., cursor_x goes from 0 to bytes_per_line
    I->cursor_x = offset % I->bytes_per_line;
    I->cursor_y = offset / I->bytes_per_line + 1;
}

void editor_move_cursor(int dir, int amount){
    for(int i = 0; i < amount; i++){
        switch (dir){
            case KEY_UP: I->cursor_y--; break;
            case KEY_DOWN: I->cursor_y++; break;
            case KEY_LEFT: I->cursor_x--; break;
            case KEY_RIGHT: I->cursor_x++; break;
        }

        // Stop top limit
        if(I->cursor_y <= 1){
            I->cursor_y = 1;
        }

        // Stop left limit
        if(I->cursor_x < 0){
            // If not the first line, move up
            if(I->cursor_y > 1){
                I->cursor_y--;
                I->cursor_x = I->bytes_per_line-1;
            }else{
                I->cursor_x = 0;
            }
        }

        // Stop right limit
        if(I->cursor_x >= I->bytes_per_line){
            I->cursor_x = 0;
            I->cursor_y++;
        }

        unsigned int offset = editor_offset_at_cursor();

        if (offset >= I->content_length - 1) {
            editor_cursor_at_offset(offset);
        }
    }

}

void editor_render_content(HEBuff* buff){

    char *asc;
    unsigned int line_chars = 0;
    unsigned int line_bytes = 0;
    unsigned int chars_until_ascii = 0;

    int row = 0;
    int col = 0;

    int offset;

    editor_calculate_bytes_per_line();

    chars_until_ascii = 10 + I->bytes_per_line * 2 + I->bytes_per_line/2;

    // Ascii
    asc = malloc(I->bytes_per_line);

    for (offset = 0; offset < I->content_length; offset++){
        // New row
        if(offset % I->bytes_per_line == 0 ){
            line_chars = 0;
            line_bytes = 0;
            buff_vappendf(buff, "%08x: ", offset);
            line_chars += 10;
            row++;
        }

        // Cursor
        if(I->cursor_y == row){
            if(I->cursor_x == line_bytes){
                buff_append(buff, "\x1b[7m", 4);
            }
        }

        // Store the value to asc buffer
        asc[offset % I->bytes_per_line] = I->file_content[offset];

        // Write the value on the screen (HEBuff)
        buff_vappendf(buff, "%02x", I->file_content[offset]);
        buff_append(buff, "\x1b[0m", 4);
        line_chars += 2;
        line_bytes += 1;

        // Every group, write a separator of len PADDING
        if(offset % I->bytes_group){
            for(int s=0; s < PADDING; s++){
                buff_append(buff, " ", 1);
                line_chars += 1;
            }
        }
        // If end of line, write ascii and new line
        if((offset + 1) % I->bytes_per_line == 0){
            editor_render_ascii(buff, row, asc, I->bytes_per_line);
            line_chars += I->bytes_per_line; // ascii chars
            buff_append(buff, "\r\n", 2);
        }
    }

    while( (chars_until_ascii - line_chars) != 0){
            buff_append(buff, " ", 1);
            line_chars++;
    }

    editor_render_ascii(buff, row, asc, line_bytes);

    free(asc);

    // padding chars

}

void editor_goto(unsigned int x, unsigned int y){
    char buffer[32];
    int w = sprintf(buffer, "\x1b[%d;%dH", y, x);
    term_print(buffer, w);
}

void editor_render_command(char *command){
    char buffer[32];
    editor_goto(I->screen_cols-10, I->screen_rows);
    int w = sprintf(buffer, "%s", command);
    term_print(buffer, w);
}

void editor_refresh_screen(){

    HEBuff* buff = I->buff;

    // Clear the content of the buffer
    buff_clear(buff);

    buff_append(buff, "\x1b[?25l", 6); // Hide cursor
    buff_append(buff, "\x1b[H", 3); // Move cursor top left


    editor_render_content(buff);

    // Clear the screen and write the buffer on it
    term_clear();
    term_draw(buff);
}


void editor_render_ruler(){
}

// Process the key pressed
void editor_process_keypress(){

    char command[MAX_COMMAND];

    // Read first command char
    int c = utils_read_key();

    // TODO: Implement key repeat correctly
    int repeat = 1;

    if(c != '0'){
        unsigned int count = 0;
        while(isdigit(c) && count < 5){
            command[count] = c;
            command[count+1] = '\0';
            editor_render_command(command);
            count++;
            repeat = atoi(command);
            c = utils_read_key();
        }
    }


    switch (c){
        case 'q':
            exit(0);
            break;
        case 'h': editor_move_cursor(KEY_LEFT, repeat); break;
        case 'j': editor_move_cursor(KEY_DOWN, repeat); break;
        case 'k': editor_move_cursor(KEY_UP, repeat); break;
        case 'l': editor_move_cursor(KEY_RIGHT, repeat); break;

        case 'w': editor_move_cursor(KEY_RIGHT, repeat*I->bytes_group); break;
        case 'b': editor_move_cursor(KEY_LEFT, repeat*I->bytes_group); break;

        // EOF
        case 'G': editor_cursor_at_offset(I->content_length-1); break;
        case 'g':
            editor_render_command("g");
            c = utils_read_key();
            if(c == 'g'){
                editor_cursor_at_offset(0);
            }
            break;
        // Start of line
        case '0': I->cursor_x = 0; break;
        // End of line
        case '$': editor_move_cursor(KEY_RIGHT, I->bytes_per_line-1 - I->cursor_x); break;
    }

}

void editor_resize(){

    // Get new terminal size
    term_get_size(I);
    // Redraw the screen
    editor_refresh_screen();
}

void editor_init(char *filename){
    I = malloc(sizeof(HEState));
    // Gets the terminal size
    term_get_size(I);
    // Enter in raw mode
    term_enable_raw(I);
    // Initialize the screen buffer
    I->buff = buff_create();

    // Set HEState variables
    I->bytes_group = 2;
    I->groups_per_line = 8;

    I->cursor_y = 1;
    I->cursor_x = 0;

    editor_open_file(filename);
    // ... file open ...
    // ........
    I->file_name = NULL;

    term_clear();
    // Register the resize handler
    signal(SIGWINCH, editor_resize);
    // Register the exit statement
    atexit(editor_exit);
}

// Clears all buffers and exits the editor
void editor_exit(){
    if(I != NULL){
        // Free the screen buff
        buff_remove(I->buff);
        if (I->file_content != NULL){
            // Free the read file content
            free(I->file_content);
        }
        if (I->file_name != NULL){
            // Free the filename
            free(I->file_name);
        }

        // Clear the screen
        term_clear();

        // Exit raw mode
        term_disable_raw(I);

        free(I);
    }
}
