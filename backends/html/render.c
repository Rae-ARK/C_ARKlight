/*
 * backends/html/render.c — Stage 4 (docs/IMPLEMENTATION.md): mirrors
 * `arklight.backend.html.render` (Rae-ARK/ARKlight, cloned separately
 * as reference), scoped to what Stages 0-3 actually carry. See
 * carklight.h's Stage 4 block comment for the full scope/deferral
 * list; the short version: five component types, no props table, no
 * routes, one output file ("index.html").
 *
 * Per ADDENDUM.md §4.1, this file only ever reaches ArkSite/
 * ArkBuildResult through carklight.h — never core/internal.h, which
 * stays private to core/ (see that file's own header comment). Every
 * string is built through the small growable buffer below rather
 * than repeated realloc-by-hand at each call site: buffer growth and
 * escaping correctness are the actual risk in this stage
 * (IMPLEMENTATION.md's own "why here" note for Stage 4), so both live
 * in one place instead of being re-derived per tag.
 */

#include "carklight.h"

#include <stdlib.h>
#include <string.h>

/* --- Growable byte buffer -------------------------------------------- */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} strbuf_t;

static int sb_init(strbuf_t* sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    if (sb->data == NULL) {
        sb->cap = 0;
        return 1;
    }
    sb->data[0] = '\0';
    return 0;
}

static void sb_free(strbuf_t* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

/* Ensures room for `extra` more bytes plus the trailing NUL. Returns
 * 0 on success; leaves `sb` unchanged (still valid, still freeable)
 * on allocation failure. */
static int sb_reserve(strbuf_t* sb, size_t extra) {
    size_t needed = sb->len + extra + 1;
    if (needed <= sb->cap) {
        return 0;
    }
    size_t new_cap = sb->cap == 0 ? 256 : sb->cap;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    char* grown = realloc(sb->data, new_cap);
    if (grown == NULL) {
        return 1;
    }
    sb->data = grown;
    sb->cap = new_cap;
    return 0;
}

static int sb_append_n(strbuf_t* sb, const char* s, size_t n) {
    if (n == 0) {
        return 0;
    }
    if (sb_reserve(sb, n) != 0) {
        return 1;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 0;
}

static int sb_append(strbuf_t* sb, const char* s) {
    return sb_append_n(sb, s, strlen(s));
}

static int sb_append_char(strbuf_t* sb, char c) {
    return sb_append_n(sb, &c, 1);
}

/* HTML-escapes `s` into `sb`, matching Python's `html.escape(s,
 * quote=True)` — the default ARKlight-py's HTML backend uses for
 * every text and attribute value it emits (see render.py's `escape`
 * import), so this port matches both content and attribute escaping
 * with the same function rather than the two Python keeps textually
 * separate. NULL is treated as the empty string. */
static int sb_append_escaped(strbuf_t* sb, const char* s) {
    if (s == NULL) {
        return 0;
    }
    for (const char* p = s; *p != '\0'; p++) {
        int rc;
        switch (*p) {
            case '&':  rc = sb_append(sb, "&amp;");  break;
            case '<':  rc = sb_append(sb, "&lt;");   break;
            case '>':  rc = sb_append(sb, "&gt;");   break;
            case '"':  rc = sb_append(sb, "&quot;"); break;
            case '\'': rc = sb_append(sb, "&#x27;"); break;
            default:   rc = sb_append_char(sb, *p);  break;
        }
        if (rc != 0) {
            return 1;
        }
    }
    return 0;
}

/* --- Node rendering ---------------------------------------------------
 * One case per Stage 0 component_type_t, via ark_ir_type() — matches
 * ARKlight-py's TAG_MAP entries for the same five types (Page ->
 * "body", handled at the document level below rather than recursed
 * into like the rest, since a Page never appears as its own child in
 * anything this port can currently build; Heading -> h<level>; Text
 * -> p; Button -> button; Container -> div).
 */

static int render_children(strbuf_t* sb, const ArkIRNode* node);

static int render_node(strbuf_t* sb, const ArkIRNode* node) {
    const char* type = ark_ir_type(node);

    if (strcmp(type, "Heading") == 0) {
        int level = ark_ir_level(node);
        if (level < 1 || level > 6) {
            level = 1; /* mirrors render.py's _tag_for fallback */
        }
        char tag[3] = {'h', (char)('0' + level), '\0'};
        if (sb_append_char(sb, '<') != 0 || sb_append(sb, tag) != 0 ||
            sb_append_char(sb, '>') != 0) {
            return 1;
        }
        if (sb_append_escaped(sb, ark_ir_text(node)) != 0) {
            return 1;
        }
        return sb_append(sb, "</") != 0 || sb_append(sb, tag) != 0 ||
               sb_append_char(sb, '>') != 0;
    }

    if (strcmp(type, "Text") == 0) {
        if (sb_append(sb, "<p>") != 0) {
            return 1;
        }
        if (sb_append_escaped(sb, ark_ir_text(node)) != 0) {
            return 1;
        }
        return sb_append(sb, "</p>");
    }

    if (strcmp(type, "Button") == 0) {
        if (sb_append(sb, "<button") != 0) {
            return 1;
        }
        const char* on_click = ark_ir_prop_on_click(node);
        if (on_click != NULL) {
            /* arklight.backend.html.render's BEHAVIOR_PROP_ATTRS maps
             * on_click -> data-ark-on-click, not a real "onclick"
             * attribute — the JS runtime (Stage 5, not ported yet)
             * reads this instead. */
            if (sb_append(sb, " data-ark-on-click=\"") != 0 ||
                sb_append_escaped(sb, on_click) != 0 ||
                sb_append_char(sb, '"') != 0) {
                return 1;
            }
        }
        if (sb_append_char(sb, '>') != 0) {
            return 1;
        }
        if (sb_append_escaped(sb, ark_ir_text(node)) != 0) {
            return 1;
        }
        return sb_append(sb, "</button>");
    }

    /* Container, and the fallback for anything else reaching here
     * (unreachable through today's public API — same defensiveness-
     * only gap ir_build.c's own component_name() fallback documents).
     * TAG_MAP.get(node.type, "div") is ARKlight-py's own default for
     * an unrecognized type, so a bare <div> is the matching choice
     * here too rather than failing the whole render. */
    if (sb_append(sb, "<div>") != 0) {
        return 1;
    }
    if (render_children(sb, node) != 0) {
        return 1;
    }
    return sb_append(sb, "</div>");
}

static int render_children(strbuf_t* sb, const ArkIRNode* node) {
    size_t n = ark_ir_child_count(node);
    for (size_t i = 0; i < n; i++) {
        if (render_node(sb, ark_ir_child_at(node, i)) != 0) {
            return 1;
        }
    }
    return 0;
}

/* --- Document rendering ------------------------------------------------
 * Mirrors render.py's _render_page, minus the stylesheet <link> and
 * <script> tags (Stage 5 — see carklight.h's Stage 4 block comment
 * for why) and minus the site_name fallback for title, since ArkSite
 * carries no site_name yet: an absent title renders as an empty
 * <title></title> rather than falling back to anything.
 */

static int render_document(strbuf_t* sb, const ArkIRNode* page_ir) {
    if (sb_append(sb,
            "<!DOCTYPE html>\n"
            "<html lang=\"en\">\n"
            "<head>\n"
            "  <meta charset=\"utf-8\">\n"
            "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "  <title>") != 0) {
        return 1;
    }
    const char* title = ark_ir_prop_title(page_ir);
    if (sb_append_escaped(sb, title != NULL ? title : "") != 0) {
        return 1;
    }
    if (sb_append(sb, "</title>\n</head>\n<body>\n") != 0) {
        return 1;
    }
    if (render_children(sb, page_ir) != 0) {
        return 1;
    }
    return sb_append(sb, "\n</body>\n</html>\n");
}

/* --- Public entry points ------------------------------------------------ */

static char* err_dup(const char* msg) {
    size_t len = strlen(msg) + 1;
    char* copy = malloc(len);
    if (copy != NULL) {
        memcpy(copy, msg, len);
    }
    return copy;
}

int ark_html_render(ArkBackend* self, const ArkSite* site,
                     ArkBuildResult* out, char** err_out) {
    (void)self; /* no per-instance state (see carklight.h's Stage 4 comment) */

    if (err_out != NULL) {
        *err_out = NULL;
    }
    if (out == NULL) {
        if (err_out != NULL) {
            *err_out = err_dup("ark_html_render: out is NULL");
        }
        return 1;
    }

    const ArkNode* root = ark_site_root(site);
    if (root == NULL) {
        return 0; /* nothing to render — not an error, same as ark_ir_build(NULL) */
    }

    ArkIRNode* ir = ark_ir_build(root);
    if (ir == NULL) {
        if (err_out != NULL) {
            *err_out = err_dup("ark_html_render: ark_ir_build failed");
        }
        return 1;
    }

    strbuf_t sb;
    if (sb_init(&sb) != 0) {
        ark_ir_free(ir);
        if (err_out != NULL) {
            *err_out = err_dup("ark_html_render: out of memory");
        }
        return 1;
    }

    int render_rc = render_document(&sb, ir);
    ark_ir_free(ir);
    if (render_rc != 0) {
        sb_free(&sb);
        if (err_out != NULL) {
            *err_out = err_dup("ark_html_render: out of memory");
        }
        return 1;
    }

    /* "/" -> "index.html", ARKlight-py's own root-route mapping — the
     * only route this port's single-root ArkSite has (see this
     * file's header comment). */
    int add_rc = ark_build_result_add_file(out, "index.html",
        (const uint8_t*)sb.data, sb.len);
    sb_free(&sb);
    if (add_rc != 0) {
        if (err_out != NULL) {
            *err_out = err_dup("ark_html_render: failed to store output file");
        }
        return 1;
    }

    return 0;
}

static ArkBackend g_html_backend = {
    "html",
    ARK_BACKEND_HTML,
    NULL,             /* init */
    ark_html_render,
    NULL,             /* postprocess */
    NULL,             /* shutdown */
};

const ArkBackend* ark_html_backend(void) {
    return &g_html_backend;
}
