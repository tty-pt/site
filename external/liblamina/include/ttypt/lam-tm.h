#ifndef LAM_TM_H
#define LAM_TM_H

/**
 * @file lam-tm.h
 * @brief Public header for the liblamina tilemap system.
 *
 * Declares the API for managing and rendering
 * 2D tilemaps in the liblamina rendering layer.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup lam_tile_types liblamina tilemap types
 *  @brief Type definitions for tilemap handling.
 *  @{
 */

/**
 * @brief Tilemap descriptor.
 *
 * Represents a subdivided texture atlas used
 * for rendering multiple tiles from a single
 * image reference.
 */
typedef struct {
	unsigned img;
	uint32_t w;
	uint32_t h;
	uint32_t nx;
	uint32_t ny;
} lam_tm_t;

/** @} */

/** @defgroup lam_tile_core liblamina tilemap creation and access
 *  @brief Tilemap management functions.
 *  @{
 */

/**
 * @brief Register a new tilemap.
 *
 * Divides the given image into fixed-size tiles.
 *
 * @param[in] img_ref Image reference (texture ID).
 * @param[in] w       Tile width.
 * @param[in] h       Tile height.
 * @return            Tilemap handle (tm_ref).
 */
uint32_t lam_tm_new(uint32_t img_ref,
		    uint32_t w,
		    uint32_t h);

/**
 * @brief Retrieve a tilemap descriptor.
 *
 * @param[in] tm_ref Tilemap handle.
 * @return           Pointer to descriptor, or NULL if invalid.
 */
const lam_tm_t *lam_tm_get(uint32_t tm_ref);

/** @} */

/** @defgroup lam_tile_render liblamina tile rendering
 *  @brief Functions for drawing individual tiles.
 *  @{
 */

/**
 * @brief Draw a single tile from a tilemap.
 *
 * @param[in] ref Tilemap handle.
 * @param[in] idx Tile index (0-based).
 * @param[in] x   Destination X position.
 * @param[in] y   Destination Y position.
 * @param[in] w   Destination width.
 * @param[in] h   Destination height.
 * @param[in] rx  Optional crop offset X (source space).
 * @param[in] ry  Optional crop offset Y (source space).
 */
void lam_tile_draw(uint32_t ref,
		   uint32_t idx,
		   uint32_t x,
		   uint32_t y,
		   uint32_t w,
		   uint32_t h,
		   uint32_t rx,
		   uint32_t ry);

/**
 * @brief Render a complete 2D grid map from an array of tile indices.
 *
 * @param[in] ref     Tilemap handle.
 * @param[in] tiles   Pointer to 2D array of tile indices (row-major, map_w * map_h entries).
 * @param[in] map_w   Width of map in tiles.
 * @param[in] map_h   Height of map in tiles.
 * @param[in] start_x Destination X coordinate on screen in pixels.
 * @param[in] start_y Destination Y coordinate on screen in pixels.
 * @param[in] scale   Integer scale multiplier for each tile (1 = 1x, 2 = 2x, etc.).
 */
void lam_tile_map_draw(uint32_t ref,
                       const uint16_t *tiles,
                       uint32_t map_w,
                       uint32_t map_h,
                       int32_t start_x,
                       int32_t start_y,
                       uint32_t scale);

#ifdef __cplusplus
}
#endif

#endif /* LAM_TM_H */
