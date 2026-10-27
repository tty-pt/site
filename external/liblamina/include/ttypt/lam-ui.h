#ifndef LAM_UI_H
#define LAM_UI_H

/**
 * @file lam-ui.h
 * @brief Public header for the liblamina UI system.
 *
 * Declares the API for liblamina’s immediate-mode
 * layout and rendering system. Provides a small,
 * CSS-like model for composing 2D interface
 * elements (divs) with flexible layout and style.
 */

#include "lam.h"
#include <stdint.h>
#include <stddef.h>

#include <ttypt/corm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Assign a style field on a div.
 *
 * Convenience macro to set a single field of a style
 * struct by name, without manually calculating offsets.
 *
 * @param[in] div   Target div.
 * @param[in] field Field name inside lam_ui_style_t.
 * @param[in] val   Field value to assign.
 */
#define LAM_UI_STYLE(div, field, val) \
	do { \
		lam_ui_style_t dummy; \
		dummy.field = val; \
		lam_ui_style_set(div, offsetof(lam_ui_style_t, field), &dummy.field, sizeof(dummy.field)); \
	} while (0)

/** @defgroup lam_ui_types liblamina UI types
 * @brief Fundamental type definitions.
 * @{
 */

/**
 * @brief Automatic layout value.
 *
 * Used for style properties like `top`, `left`, `flex_basis`,
 * etc., to indicate that the layout engine should
 * determine the value automatically.
 */
static const int32_t LAM_UI_AUTO = -1;

/**
 * @brief Layout direction for container elements (CSS: flex-direction).
 */
typedef enum {
	LAM_UI_COLUMN,
	LAM_UI_ROW,
} lam_ui_flex_direction_t;

/**
 * @brief Positioning mode for elements (CSS: position).
 */
typedef enum {
	LAM_UI_POSITION_RELATIVE,
	LAM_UI_POSITION_ABSOLUTE,
} lam_ui_position_mode_t;

/**
 * @brief Display mode for visibility control (CSS: display).
 */
typedef enum {
	LAM_UI_DISPLAY_BLOCK,
	LAM_UI_DISPLAY_INLINE,
	LAM_UI_DISPLAY_FLEX,
	LAM_UI_DISPLAY_NONE,
} lam_ui_display_mode_t;

/**
 * @brief Alignment along cross axis (CSS: align-items, align-self).
 */
typedef enum {
	LAM_UI_ALIGN_STRETCH = 0,
	LAM_UI_ALIGN_FLEX_START,
	LAM_UI_ALIGN_CENTER,
	LAM_UI_ALIGN_FLEX_END,
	LAM_UI_ALIGN_AUTO = -1,
} lam_ui_align_mode_t;

/**
 * @brief Alignment of items along the main axis (CSS: justify-content).
 */
typedef enum {
	LAM_UI_JUSTIFY_FLEX_START = 0,
	LAM_UI_JUSTIFY_CENTER,
	LAM_UI_JUSTIFY_FLEX_END,
	LAM_UI_JUSTIFY_SPACE_BETWEEN,
	LAM_UI_JUSTIFY_SPACE_AROUND
} lam_ui_justify_content_mode_t;

/**
 * @brief Text alignment mode (CSS: text-align).
 */
typedef enum {
	LAM_UI_TEXT_ALIGN_LEFT = 0,
	LAM_UI_TEXT_ALIGN_CENTER,
	LAM_UI_TEXT_ALIGN_RIGHT
} lam_ui_text_align_t;

/**
 * @brief White-space handling (CSS: white-space).
 */
typedef enum {
	LAM_UI_WS_NORMAL = 0,
	LAM_UI_WS_NOWRAP,
	LAM_UI_WS_PRE,
	LAM_UI_WS_PRE_WRAP,
	LAM_UI_WS_PRE_LINE
} lam_ui_white_space_t;

/**
 * @brief Word breaking behavior (CSS: word-break).
 */
typedef enum {
	LAM_UI_WB_NORMAL = 0,
	LAM_UI_WB_BREAK_ALL,
	LAM_UI_WB_KEEP_ALL,
	LAM_UI_WB_BREAK_WORD
} lam_ui_word_break_t;

/**
 * @brief Overflow handling for the vertical axis.
 */
typedef enum {
	LAM_UI_OVERFLOW_HIDDEN = 0,
	LAM_UI_OVERFLOW_VISIBLE,
} lam_ui_overflow_t;

/**
 * @brief Visual style definition for a UI element.
 *
 * Represents colors, borders, fonts and layout
 * metrics. Styles may be defined globally in a
 * stylesheet and applied by class name.
 */
typedef struct {
	uint32_t background_color;
	uint32_t border_color;
	uint32_t border_width;
	uint32_t background_image_ref;
	uint32_t font_family_ref;
	uint32_t font_size;
	uint32_t color;

	uint32_t padding_left;
	uint32_t padding_right;
	uint32_t padding_top;
	uint32_t padding_bottom;

	/* positioning */
	lam_ui_position_mode_t position;
	int32_t  top;
	int32_t  left;
	int32_t  right;
	int32_t  bottom;

	lam_ui_display_mode_t  display;

	/* flexbox layout */
	lam_ui_flex_direction_t flex_direction;
	float flex_grow;
	float flex_shrink;
	int32_t flex_basis;

	/* flex alignment properties */
	lam_ui_align_mode_t   align_items;
	lam_ui_align_mode_t   align_self;
	lam_ui_justify_content_mode_t justify_content;

	uint32_t border_radius_top_left;
	uint32_t border_radius_top_right;
	uint32_t border_radius_bottom_right;
	uint32_t border_radius_bottom_left;

	uint32_t box_shadow_color;
	float    box_shadow_blur;
	float    box_shadow_offset_x;
	float    box_shadow_offset_y;

	lam_ui_text_align_t text_align;
	lam_ui_white_space_t white_space;
	lam_ui_word_break_t  word_break;

	int32_t width, height;

	lam_ui_overflow_t overflow_y;
} lam_ui_style_t;

/**
 * @brief Opaque UI element.
 *
 * Each div represents a node in the layout tree.
 */
typedef struct lam_ui_div lam_ui_div_t;

/** @} */

/** @defgroup lam_ui_core liblamina UI initialization and stylesheet
 * @brief Core setup and stylesheet management.
 * @{
 */

/**
 * @brief Initialize the UI subsystem.
 */
void lam_ui_init(uint32_t screen_w, uint32_t screen_h);

/**
 * @brief Initialize (or reset) a stylesheet.
 */
uint32_t lam_ui_stylesheet_init(void);

/**
 * @brief Add a new class rule to a stylesheet.
 */
static inline void lam_ui_stylesheet_add(uint32_t ss,
			const char *class_name,
			const lam_ui_style_t *style)
{
	corm_put(ss, class_name, style);
}

/** @} */

/** @defgroup lam_ui_tree liblamina UI element tree
 * @brief Element creation and configuration.
 * @{
 */

/**
 * @brief Create a new UI element.
 */
lam_ui_div_t *lam_ui_new(lam_ui_div_t *parent, lam_ui_style_t *style);

/**
 * @brief Create a flexbox container element with display: flex and direction.
 *
 * @param[in] parent Parent element or NULL for root.
 * @param[in] dir    Flex direction (LAM_UI_ROW or LAM_UI_COLUMN).
 * @return           Pointer to new element.
 */
lam_ui_div_t *lam_ui_flex(lam_ui_div_t *parent, lam_ui_flex_direction_t dir);

/**
 * @brief Create a sized box element with background color.
 *
 * @param[in] parent   Parent element or NULL for root.
 * @param[in] w,h      Width and height in pixels (or LAM_UI_AUTO).
 * @param[in] bg_color Background color (packed RGBA).
 * @return             Pointer to new element.
 */
lam_ui_div_t *lam_ui_box(lam_ui_div_t *parent, int32_t w, int32_t h, uint32_t bg_color);

/**
 * @brief Create a text label element with font family and text color.
 *
 * @param[in] parent   Parent element or NULL for root.
 * @param[in] text     UTF-8 text string.
 * @param[in] font_ref Font handle (from lam_font_open).
 * @param[in] color    Text color (packed RGBA).
 * @return             Pointer to new element.
 */
lam_ui_div_t *lam_ui_label(lam_ui_div_t *parent, const char *text, uint32_t font_ref, uint32_t color);

/**
 * @brief Set explicit width and height dimensions on a div.
 */
void lam_ui_set_size(lam_ui_div_t *div, int32_t w, int32_t h);

/**
 * @brief Set background color on a div.
 */
void lam_ui_set_bg(lam_ui_div_t *div, uint32_t color);

/**
 * @brief Set padding on all four edges of a div.
 */
void lam_ui_set_padding(lam_ui_div_t *div, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom);

/**
 * @brief Set flex properties (grow, shrink, basis) on a div.
 */
void lam_ui_set_flex(lam_ui_div_t *div, float grow, float shrink, int32_t basis);

/**
 * @brief Set alignment properties on a flex container.
 */
void lam_ui_set_align(lam_ui_div_t *div, lam_ui_align_mode_t align_items, lam_ui_justify_content_mode_t justify_content);

/**
 * @brief Assign a CSS-like class to a div.
 */
void lam_ui_class(lam_ui_div_t *div, const char *class_name);

/**
 * @brief Assign text content to a div.
 */
void lam_ui_text(lam_ui_div_t *div, const char *text);

/**
 * @brief Query overflow continuation text.
 */
const char *lam_ui_overflow(const lam_ui_div_t *div);

/** @} */

/** @defgroup lam_ui_layout liblamina UI layout and rendering
 * @brief Applying styles, computing geometry and drawing.
 * @{
 */

/**
 * @brief Apply stylesheet rules and compute final styles.
 */
void lam_ui_apply_styles(lam_ui_div_t *root, uint32_t ss);

/**
 * @brief Compute layout positions and sizes.
 */
void lam_ui_layout(lam_ui_div_t *root);

/**
 * @brief Render the element tree.
 */
void lam_ui_render(lam_ui_div_t *root);

/**
 * @brief Recursively free a UI tree.
 */
void lam_ui_clear(lam_ui_div_t *root);

/**
 * @brief Reset a style struct to its default values.
 */
void lam_ui_style_reset(lam_ui_style_t *s);

/**
 * @brief Draw a rectangle with rounded corners and optional border.
 */
void lam_border_radius(uint32_t background_color,
                       uint32_t border_color,
                       int32_t x, int32_t y,
                       uint32_t w, uint32_t h,
                       float tl, float tr,
                       float br, float bl,
                       float border_width);

/**
 * @brief Draw a blurred drop shadow behind a rounded box.
 */
void lam_box_shadow(uint32_t color,
		    int32_t x, int32_t y,
		    uint32_t w, uint32_t h,
		    float tl, float tr, float br, float bl,
		    float blur_radius, float offset_x, float offset_y);

/**
 * @brief Mark a div as dirty so its cached render is rebuilt.
 */
void lam_ui_mark_dirty(lam_ui_div_t *d);

/**
 * @brief Set an inline style value by byte offset.
 */
void lam_ui_style_set(lam_ui_div_t *root, size_t offset,
		      const void *value, size_t size);

/**
 * @brief Get computed X position of a div after layout.
 */
int32_t lam_ui_get_x(const lam_ui_div_t *div);

/**
 * @brief Get computed Y position of a div after layout.
 */
int32_t lam_ui_get_y(const lam_ui_div_t *div);

/**
 * @brief Get computed width of a div after layout.
 */
int32_t lam_ui_get_width(const lam_ui_div_t *div);

/**
 * @brief Get computed height of a div after layout.
 */
int32_t lam_ui_get_height(const lam_ui_div_t *div);

/**
 * @brief Get content width of a div after layout.
 */
uint32_t lam_ui_get_content_width(const lam_ui_div_t *div);

/**
 * @brief Get content height of a div after layout.
 */
uint32_t lam_ui_get_content_height(const lam_ui_div_t *div);

/**
 * @brief Get text content of a div.
 */
const char *lam_ui_get_text(const lam_ui_div_t *div);

/**
 * @brief Get class name of a div.
 */
const char *lam_ui_get_class(const lam_ui_div_t *div);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LAM_UI_H */
