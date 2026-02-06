#include "scroll.h"

void scroll_reset(scroll_state_t *s) {
    s->total_items = 0;
    s->visible_rows = 0;
    s->cursor = 0;
    s->scroll_offset = 0;
}

void scroll_ensure_visible(scroll_state_t *s) {
    if (s->total_items <= 0) {
        s->cursor = 0;
        s->scroll_offset = 0;
        return;
    }

    /* Cursor above visible area -- scroll up */
    if (s->cursor < s->scroll_offset) {
        s->scroll_offset = s->cursor;
    }
    /* Cursor below visible area -- scroll down */
    if (s->cursor >= s->scroll_offset + s->visible_rows) {
        s->scroll_offset = s->cursor - s->visible_rows + 1;
    }
    /* Clamp scroll offset */
    int max_offset = s->total_items - s->visible_rows;
    if (max_offset < 0) max_offset = 0;
    if (s->scroll_offset > max_offset) s->scroll_offset = max_offset;
    if (s->scroll_offset < 0) s->scroll_offset = 0;
}

void scroll_move(scroll_state_t *s, int delta) {
    if (s->total_items <= 0) return;

    s->cursor += delta;
    if (s->cursor < 0) s->cursor = 0;
    if (s->cursor >= s->total_items) s->cursor = s->total_items - 1;
    scroll_ensure_visible(s);
}

void scroll_page(scroll_state_t *s, int direction) {
    if (s->total_items <= 0) return;

    int page_size = s->visible_rows > 0 ? s->visible_rows : 1;
    scroll_move(s, direction * page_size);
}

void scroll_to_top(scroll_state_t *s) {
    s->cursor = 0;
    s->scroll_offset = 0;
}

void scroll_to_bottom(scroll_state_t *s) {
    if (s->total_items <= 0) return;

    s->cursor = s->total_items - 1;
    scroll_ensure_visible(s);
}
