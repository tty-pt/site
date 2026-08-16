#ifndef MUSIC_H
#define MUSIC_H

const char *key_name(int semitones, int orig_key, int latin);
const char *target_key_name(int orig_key, int transpose, int latin);

#endif
