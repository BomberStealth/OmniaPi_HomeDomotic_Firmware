#ifndef EFFECTS_H
#define EFFECTS_H

#include <stdint.h>
#include <stdbool.h>

// ============================================
// EFFECT TYPES
// ============================================

typedef enum {
    EFFECT_TYPE_STATIC = 0,
    EFFECT_TYPE_RAINBOW,
    EFFECT_TYPE_BREATHING,
    EFFECT_TYPE_CHASE,
    EFFECT_TYPE_SPARKLE,
    EFFECT_TYPE_FIRE,
    EFFECT_TYPE_CUSTOM,     // Custom 3-color rainbow
    EFFECT_TYPE_MAX
} effect_type_t;

// ============================================
// EFFECT CONTEXT
// ============================================

typedef struct {
    effect_type_t type;
    uint8_t speed;          // 0-255
    uint8_t r, g, b;        // Base color
    uint8_t brightness;     // Master brightness

    // Internal state for animations
    uint32_t step;          // Animation step counter
    uint32_t last_update;   // Last update timestamp (ms)

    // Custom effect colors (3 colors for custom rainbow)
    uint8_t custom_r1, custom_g1, custom_b1;
    uint8_t custom_r2, custom_g2, custom_b2;
    uint8_t custom_r3, custom_g3, custom_b3;
} effect_ctx_t;

// ============================================
// FUNCTION PROTOTYPES
// ============================================

/**
 * Initialize effect system
 */
void effects_init(void);

/**
 * Set effect type
 */
void effects_set_type(effect_type_t type);

/**
 * Set effect speed (0-255)
 */
void effects_set_speed(uint8_t speed);

/**
 * Set base color for effects
 */
void effects_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * Set master brightness
 */
void effects_set_brightness(uint8_t brightness);

/**
 * Update effect animation (call in main loop)
 * @return true if LEDs were updated and need refresh
 */
bool effects_update(void);

/**
 * Get effect context (for state reporting)
 */
effect_ctx_t* effects_get_ctx(void);

/**
 * Reset effect animation state
 */
void effects_reset(void);

/**
 * Set number of LEDs (called when num_leds changes)
 */
void effects_set_num_leds(uint16_t num);

// ============================================
// INDIVIDUAL EFFECT FUNCTIONS (internal)
// ============================================

void effect_static(effect_ctx_t* ctx);
void effect_rainbow(effect_ctx_t* ctx);
void effect_breathing(effect_ctx_t* ctx);
void effect_chase(effect_ctx_t* ctx);
void effect_sparkle(effect_ctx_t* ctx);
void effect_fire(effect_ctx_t* ctx);
void effect_custom_rainbow(effect_ctx_t* ctx);

/**
 * Set custom effect colors (3 RGB colors)
 */
void effects_set_custom_colors(uint8_t r1, uint8_t g1, uint8_t b1,
                               uint8_t r2, uint8_t g2, uint8_t b2,
                               uint8_t r3, uint8_t g3, uint8_t b3);

#endif // EFFECTS_H
