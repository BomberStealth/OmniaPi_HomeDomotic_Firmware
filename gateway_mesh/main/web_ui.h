/**
 * OmniaPi Gateway Mesh - Web UI
 *
 * Embedded HTML/CSS/JS for gateway web interface
 */

#ifndef WEB_UI_H
#define WEB_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the main HTML page
 */
const char* web_ui_get_html(void);

/**
 * Get the CSS stylesheet
 */
const char* web_ui_get_css(void);

/**
 * Get the JavaScript application
 */
const char* web_ui_get_js(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_UI_H
