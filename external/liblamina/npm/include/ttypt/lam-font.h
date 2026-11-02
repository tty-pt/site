#ifndef LAM_FONT_H
#define LAM_FONT_H

/**
 * @file lam-font.h
 * @brief Public header for the liblamina bitmap font system.
 *
 * Declares the API for loading, measuring and rendering
 * bitmap-based fonts used by the liblamina rendering layer.
 */

#include <stdint.h>
#include "./lam-ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup lam_font_core liblamina font management
 *  @brief Font loading and lifecycle functions.
 *  @{
 */

/**
 * @brief Load a bitmap font from an image file.
 *
 * Loads a pre-rendered font atlas (e.g. generated from BDF)
 * and registers it in liblamina’s internal font table.
 *
 * @param[in] png_path Path to the font atlas (PNG).
 * @param[in] cell_w   Width of each glyph cell.
 * @param[in] cell_h   Height of each glyph cell.
 * @param[in] first    First character code in the atlas.
 * @param[in] last     Last character code in the atlas.
 * @return Font handle (font_ref) or CM_MISS on error.
 */
uint32_t lam_font_open(const char *png_path,
		       unsigned cell_w,
		       unsigned cell_h,
		       uint8_t first,
		       uint8_t last);

/**
 * @brief Unload a font and free its resources.
 *
 * @param[in] font_ref Font handle returned by lam_font_open().
 */
void lam_font_close(uint32_t font_ref);

/** @} */

/** @defgroup lam_font_render liblamina font rendering and measurement
 *  @brief Functions for drawing and measuring text.
 *  @{
 */

/**
 * @brief Draw text using a loaded bitmap font.
 *
 * Renders text within the given rectangle, obeying wrapping
 * and breaking rules. Returns a pointer to the first
 * unrendered character if overflow occurred.
 *
 * @param[in] font_ref    Font handle.
 * @param[in] text        UTF-8 text to render.
 * @param[in] x0,y0       Top-left position (pixels).
 * @param[in] x1,y1       Bottom-right boundary (pixels).
 * @param[in] scale       Font scale factor.
 * @param[in] white_space White-space handling mode (see lam_ui_white_space_t).
 * @param[in] word_break  Word-break behavior (see lam_ui_word_break_t).
 * @return NULL if fully rendered, or pointer to remaining text.
 */
const char *lam_font_draw(uint32_t font_ref,
			  const char *text,
			  uint32_t x0,
			  uint32_t y0,
			  uint32_t x1,
			  uint32_t y1,
			  uint32_t scale,
			  lam_ui_white_space_t white_space,
			  lam_ui_word_break_t word_break);

/**
 * @brief Measure text layout and overflow.
 *
 * Computes how text fits within the specified rectangle
 * and returns a pointer to the first unrendered character
 * if overflow occurred.
 *
 * @param[out] out_w       Optional: resulting text width in pixels.
 * @param[out] out_h       Optional: resulting text height in pixels.
 * @param[in]  font_ref    Font handle.
 * @param[in]  text        UTF-8 text to measure.
 * @param[in]  x0,y0       Top-left position (pixels).
 * @param[in]  x1,y1       Bottom-right boundary (pixels).
 * @param[in]  scale       Font scale factor.
 * @param[in]  white_space White-space handling mode (see lam_ui_white_space_t).
 * @param[in]  word_break  Word-break behavior (see lam_ui_word_break_t).
 * @return NULL if text fully fits, or pointer to remaining text.
 */
const char *lam_font_measure(uint32_t *out_w,
			     uint32_t *out_h,
			     uint32_t font_ref,
			     const char *text,
			     uint32_t x0,
			     uint32_t y0,
			     uint32_t x1,
			     uint32_t y1,
			     uint32_t scale,
			     lam_ui_white_space_t white_space,
			     lam_ui_word_break_t word_break);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LAM_FONT_H */
