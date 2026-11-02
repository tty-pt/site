#ifndef LAM_KEY_HEADER
#define LAM_KEY_HEADER

#define LAM_PRESS                  1
#define LAM_REPEAT                 2

#define LAM_HAT_CENTERED           0
#define LAM_HAT_UP                 1
#define LAM_HAT_RIGHT              2
#define LAM_HAT_DOWN               4
#define LAM_HAT_LEFT               8
#define LAM_HAT_RIGHT_UP           (LAM_HAT_RIGHT | LAM_HAT_UP)
#define LAM_HAT_RIGHT_DOWN         (LAM_HAT_RIGHT | LAM_HAT_DOWN)
#define LAM_HAT_LEFT_UP            (LAM_HAT_LEFT  | LAM_HAT_UP)
#define LAM_HAT_LEFT_DOWN          (LAM_HAT_LEFT  | LAM_HAT_DOWN)

#define LAM_KEY_UNKNOWN		-1

#define LAM_KEY_SPACE              32
#define LAM_KEY_APOSTROPHE         39  /* ' */
#define LAM_KEY_COMMA              44  /* , */
#define LAM_KEY_MINUS              45  /* - */
#define LAM_KEY_PERIOD             46  /* . */
#define LAM_KEY_SLASH              47  /* / */
#define LAM_KEY_0                  48
#define LAM_KEY_1                  49
#define LAM_KEY_2                  50
#define LAM_KEY_3                  51
#define LAM_KEY_4                  52
#define LAM_KEY_5                  53
#define LAM_KEY_6                  54
#define LAM_KEY_7                  55
#define LAM_KEY_8                  56
#define LAM_KEY_9                  57
#define LAM_KEY_SEMICOLON          59  /* ; */
#define LAM_KEY_EQUAL              61  /* = */
#define LAM_KEY_A                  65
#define LAM_KEY_B                  66
#define LAM_KEY_C                  67
#define LAM_KEY_D                  68
#define LAM_KEY_E                  69
#define LAM_KEY_F                  70
#define LAM_KEY_G                  71
#define LAM_KEY_H                  72
#define LAM_KEY_I                  73
#define LAM_KEY_J                  74
#define LAM_KEY_K                  75
#define LAM_KEY_L                  76
#define LAM_KEY_M                  77
#define LAM_KEY_N                  78
#define LAM_KEY_O                  79
#define LAM_KEY_P                  80
#define LAM_KEY_Q                  81
#define LAM_KEY_R                  82
#define LAM_KEY_S                  83
#define LAM_KEY_T                  84
#define LAM_KEY_U                  85
#define LAM_KEY_V                  86
#define LAM_KEY_W                  87
#define LAM_KEY_X                  88
#define LAM_KEY_Y                  89
#define LAM_KEY_Z                  90
#define LAM_KEY_LEFT_BRACKET       91  /* [ */
#define LAM_KEY_BACKSLASH          92  /* \ */
#define LAM_KEY_RIGHT_BRACKET      93  /* ] */
#define LAM_KEY_GRAVE_ACCENT       96  /* ` */
#define LAM_KEY_WORLD_1            161 /* non-US #1 */
#define LAM_KEY_WORLD_2            162 /* non-US #2 */

/* Function keys */
#define LAM_KEY_ESCAPE             256
#define LAM_KEY_ENTER              257
#define LAM_KEY_TAB                258
#define LAM_KEY_BACKSPACE          259
#define LAM_KEY_INSERT             260
#define LAM_KEY_DELETE             261
#define LAM_KEY_RIGHT              262
#define LAM_KEY_LEFT               263
#define LAM_KEY_DOWN               264
#define LAM_KEY_UP                 265
#define LAM_KEY_PAGE_UP            266
#define LAM_KEY_PAGE_DOWN          267
#define LAM_KEY_HOME               268
#define LAM_KEY_END                269
#define LAM_KEY_CAPS_LOCK          280
#define LAM_KEY_SCROLL_LOCK        281
#define LAM_KEY_NUM_LOCK           282
#define LAM_KEY_PRINT_SCREEN       283
#define LAM_KEY_PAUSE              284
#define LAM_KEY_F1                 290
#define LAM_KEY_F2                 291
#define LAM_KEY_F3                 292
#define LAM_KEY_F4                 293
#define LAM_KEY_F5                 294
#define LAM_KEY_F6                 295
#define LAM_KEY_F7                 296
#define LAM_KEY_F8                 297
#define LAM_KEY_F9                 298
#define LAM_KEY_F10                299
#define LAM_KEY_F11                300
#define LAM_KEY_F12                301
#define LAM_KEY_F13                302
#define LAM_KEY_F14                303
#define LAM_KEY_F15                304
#define LAM_KEY_F16                305
#define LAM_KEY_F17                306
#define LAM_KEY_F18                307
#define LAM_KEY_F19                308
#define LAM_KEY_F20                309
#define LAM_KEY_F21                310
#define LAM_KEY_F22                311
#define LAM_KEY_F23                312
#define LAM_KEY_F24                313
#define LAM_KEY_F25                314
#define LAM_KEY_KP_0               320
#define LAM_KEY_KP_1               321
#define LAM_KEY_KP_2               322
#define LAM_KEY_KP_3               323
#define LAM_KEY_KP_4               324
#define LAM_KEY_KP_5               325
#define LAM_KEY_KP_6               326
#define LAM_KEY_KP_7               327
#define LAM_KEY_KP_8               328
#define LAM_KEY_KP_9               329
#define LAM_KEY_KP_DECIMAL         330
#define LAM_KEY_KP_DIVIDE          331
#define LAM_KEY_KP_MULTIPLY        332
#define LAM_KEY_KP_SUBTRACT        333
#define LAM_KEY_KP_ADD             334
#define LAM_KEY_KP_ENTER           335
#define LAM_KEY_KP_EQUAL           336
#define LAM_KEY_LEFT_SHIFT         340
#define LAM_KEY_LEFT_CONTROL       341
#define LAM_KEY_LEFT_ALT           342
#define LAM_KEY_LEFT_SUPER         343
#define LAM_KEY_RIGHT_SHIFT        344
#define LAM_KEY_RIGHT_CONTROL      345
#define LAM_KEY_RIGHT_ALT          346
#define LAM_KEY_RIGHT_SUPER        347
#define LAM_KEY_MENU               348

#define LAM_KEY_LAST               LAM_KEY_MENU

#define LAM_MOD_SHIFT           0x0001
#define LAM_MOD_CONTROL         0x0002
#define LAM_MOD_SUPER           0x0002
#define LAM_MOD_CAPS_LOCK       0x0010
#define LAM_MOD_NUM_LOCK        0x0020

#define LAM_MOUSE_BUTTON_1         0
#define LAM_MOUSE_BUTTON_2         1
#define LAM_MOUSE_BUTTON_3         2
#define LAM_MOUSE_BUTTON_4         3
#define LAM_MOUSE_BUTTON_5         4
#define LAM_MOUSE_BUTTON_6         5
#define LAM_MOUSE_BUTTON_7         6
#define LAM_MOUSE_BUTTON_8         7
#define LAM_MOUSE_BUTTON_LAST      LAM_MOUSE_BUTTON_8
#define LAM_MOUSE_BUTTON_LEFT      LAM_MOUSE_BUTTON_1
#define LAM_MOUSE_BUTTON_RIGHT     LAM_MOUSE_BUTTON_2
#define LAM_MOUSE_BUTTON_MIDDLE    LAM_MOUSE_BUTTON_3

#define LAM_JOYSTICK_1             0
#define LAM_JOYSTICK_2             1
#define LAM_JOYSTICK_3             2
#define LAM_JOYSTICK_4             3
#define LAM_JOYSTICK_5             4
#define LAM_JOYSTICK_6             5
#define LAM_JOYSTICK_7             6
#define LAM_JOYSTICK_8             7
#define LAM_JOYSTICK_9             8
#define LAM_JOYSTICK_10            9
#define LAM_JOYSTICK_11            10
#define LAM_JOYSTICK_12            11
#define LAM_JOYSTICK_13            12
#define LAM_JOYSTICK_14            13
#define LAM_JOYSTICK_15            14
#define LAM_JOYSTICK_16            15
#define LAM_JOYSTICK_LAST          LAM_JOYSTICK_16

#define LAM_GAMEPAD_BUTTON_A               0
#define LAM_GAMEPAD_BUTTON_B               1
#define LAM_GAMEPAD_BUTTON_X               2
#define LAM_GAMEPAD_BUTTON_Y               3
#define LAM_GAMEPAD_BUTTON_LEFT_BUMPER     4
#define LAM_GAMEPAD_BUTTON_RIGHT_BUMPER    5
#define LAM_GAMEPAD_BUTTON_BACK            6
#define LAM_GAMEPAD_BUTTON_START           7
#define LAM_GAMEPAD_BUTTON_GUIDE           8
#define LAM_GAMEPAD_BUTTON_LEFT_THUMB      9
#define LAM_GAMEPAD_BUTTON_RIGHT_THUMB     10
#define LAM_GAMEPAD_BUTTON_DPAD_UP         11
#define LAM_GAMEPAD_BUTTON_DPAD_RIGHT      12
#define LAM_GAMEPAD_BUTTON_DPAD_DOWN       13
#define LAM_GAMEPAD_BUTTON_DPAD_LEFT       14
#define LAM_GAMEPAD_BUTTON_LAST            LAM_GAMEPAD_BUTTON_DPAD_LEFT

#define LAM_GAMEPAD_BUTTON_CROSS       LAM_GAMEPAD_BUTTON_A
#define LAM_GAMEPAD_BUTTON_CIRCLE      LAM_GAMEPAD_BUTTON_B
#define LAM_GAMEPAD_BUTTON_SQUARE      LAM_GAMEPAD_BUTTON_X
#define LAM_GAMEPAD_BUTTON_TRIANGLE    LAM_GAMEPAD_BUTTON_Y

#define LAM_GAMEPAD_AXIS_LEFT_X        0
#define LAM_GAMEPAD_AXIS_LEFT_Y        1
#define LAM_GAMEPAD_AXIS_RIGHT_X       2
#define LAM_GAMEPAD_AXIS_RIGHT_Y       3
#define LAM_GAMEPAD_AXIS_LEFT_TRIGGER  4
#define LAM_GAMEPAD_AXIS_RIGHT_TRIGGER 5
#define LAM_GAMEPAD_AXIS_LAST          LAM_GAMEPAD_AXIS_RIGHT_TRIGGER

#endif /* LAM_KEY_HEADER */
