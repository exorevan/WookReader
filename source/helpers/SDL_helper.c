#include "common.h"
#include "SDL_helper.h"
#include <SDL2/SDL_image.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <string.h>

// ── Text texture cache ────────────────────────────────────────────────────────
// Eliminates the create+destroy SDL_Texture cycle that previously happened on
// every SDL_DrawText / SDL_DrawRotatedText call (~300×/sec in typical use).

#define TEXT_CACHE_MAX 64
#define TEXT_CACHE_KEY_LEN 128

typedef struct {
    TTF_Font   *font;
    SDL_Color   color;
    char        text[TEXT_CACHE_KEY_LEN];
} TextCacheKey;

typedef struct {
    TextCacheKey key;
    SDL_Texture *tex;
    int          w, h;
    int          stamp;   // g_tc_frame value at last use — for LRU eviction
    int          used;    // 0 = empty slot
} TextCacheEntry;

static TextCacheEntry g_tc[TEXT_CACHE_MAX];
static int            g_tc_frame = 0;

void SDL_TextCache_NextFrame(void) { g_tc_frame++; }

void SDL_TextCache_Clear(void) {
    for (int i = 0; i < TEXT_CACHE_MAX; i++) {
        if (g_tc[i].used && g_tc[i].tex) {
            SDL_DestroyTexture(g_tc[i].tex);
            g_tc[i].tex  = NULL;
            g_tc[i].used = 0;
        }
    }
}

// Returns a cached SDL_Texture* for (font, color, text), creating one if needed.
// Stores the texture dimensions in *out_w / *out_h.
static SDL_Texture *tc_get(SDL_Renderer *renderer, TTF_Font *font,
                            SDL_Color color, const char *text,
                            int *out_w, int *out_h)
{
    if (!text || !font) return NULL;

    // Linear search for matching entry
    int lru_idx   = 0;
    int lru_stamp = g_tc[0].stamp;

    for (int i = 0; i < TEXT_CACHE_MAX; i++) {
        if (!g_tc[i].used) {
            // Empty slot — use it directly as the candidate for insertion
            lru_idx = i;
            lru_stamp = -1;  // prefer empty over any used slot
            break;
        }
        // Check for exact match
        if (g_tc[i].key.font == font &&
            g_tc[i].key.color.r == color.r &&
            g_tc[i].key.color.g == color.g &&
            g_tc[i].key.color.b == color.b &&
            g_tc[i].key.color.a == color.a &&
            strncmp(g_tc[i].key.text, text, TEXT_CACHE_KEY_LEN - 1) == 0)
        {
            g_tc[i].stamp = g_tc_frame;
            *out_w = g_tc[i].w;
            *out_h = g_tc[i].h;
            return g_tc[i].tex;
        }
        // Track LRU candidate
        if (g_tc[i].stamp < lru_stamp) {
            lru_stamp = g_tc[i].stamp;
            lru_idx   = i;
        }
    }

    // Cache miss — create new texture
    SDL_Surface *surf = TTF_RenderText_Blended_Wrapped(font, text, color, 1280);
    if (!surf) return NULL;
    SDL_SetSurfaceAlphaMod(surf, color.a);
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    int w = surf->w, h = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return NULL;

    // Evict LRU slot
    if (g_tc[lru_idx].used && g_tc[lru_idx].tex)
        SDL_DestroyTexture(g_tc[lru_idx].tex);

    g_tc[lru_idx].used      = 1;
    g_tc[lru_idx].tex       = tex;
    g_tc[lru_idx].w         = w;
    g_tc[lru_idx].h         = h;
    g_tc[lru_idx].stamp     = g_tc_frame;
    g_tc[lru_idx].key.font  = font;
    g_tc[lru_idx].key.color = color;
    strncpy(g_tc[lru_idx].key.text, text, TEXT_CACHE_KEY_LEN - 1);
    g_tc[lru_idx].key.text[TEXT_CACHE_KEY_LEN - 1] = '\0';

    *out_w = w;
    *out_h = h;
    return tex;
}

void SDL_ClearScreen(SDL_Renderer *renderer, SDL_Color colour)
{
	SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
	SDL_RenderClear(renderer);
}

void SDL_DrawRect(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color colour)
{
	SDL_Rect rect;
	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;
	SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
	SDL_RenderFillRect(renderer, &rect);
}

void SDL_DrawCircle(SDL_Renderer *renderer, int x, int y, int r, SDL_Color colour)
{
	filledCircleRGBA(renderer, x, y, r, colour.r, colour.g, colour.b, colour.a);
	return;
}

void SDL_DrawText(SDL_Renderer *renderer, TTF_Font *font, int x, int y, SDL_Color colour, const char *text)
{
	int w = 0, h = 0;
	SDL_Texture *texture = tc_get(renderer, font, colour, text, &w, &h);
	if (!texture) return;

	SDL_Rect position = { x, y, w, h };
	SDL_RenderCopy(renderer, texture, NULL, &position);
}

void SDL_DrawRotatedText(SDL_Renderer *renderer, TTF_Font *font, double rotation, int x, int y, SDL_Color colour, const char *text)
{
	int w = 0, h = 0;
	SDL_Texture *texture = tc_get(renderer, font, colour, text, &w, &h);
	if (!texture) return;

	SDL_Rect position = { x, y, w, h };
	SDL_RenderCopyEx(renderer, texture, NULL, &position, rotation, NULL, SDL_FLIP_NONE);
}

void SDL_DrawTextWrapped(SDL_Renderer *renderer, TTF_Font *font, int x, int y, int wrapWidth, int maxH, SDL_Color colour, const char *text)
{
	if (!text || !font) return;
	SDL_Surface *surf = TTF_RenderUTF8_Blended_Wrapped(font, text, colour, (Uint32)wrapWidth);
	if (!surf) return;
	SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
	int w = surf->w, h = surf->h;
	SDL_FreeSurface(surf);
	if (!tex) return;
	int clip_h = (maxH > 0 && h > maxH) ? maxH : h;
	SDL_Rect src = { 0, 0, w, clip_h };
	SDL_Rect dst = { x, y, w, clip_h };
	SDL_RenderCopy(renderer, tex, &src, &dst);
	SDL_DestroyTexture(tex);
}

void SDL_DrawTextf(SDL_Renderer *renderer, TTF_Font *font, int x, int y, SDL_Color colour, const char *text, ...)
{
	char buffer[256];
	va_list args;
	va_start(args, text);
	vsnprintf(buffer, 256, text, args);
	SDL_DrawText(renderer, font, x, y, colour, buffer);
	va_end(args);
}

void SDL_LoadImage(SDL_Texture **texture, char *path)
{
	SDL_Surface *image = NULL;

	image = IMG_Load(path);
	if (!image)
	{
		// DEBUG_LOG("IMG_Load failed: %s\n", IMG_GetError());
		printf("IMG_Load failed: %s\n", IMG_GetError());

		return;
	}

	SDL_Surface *converted = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_RGBA8888, 0);
	if (converted)
	{
		SDL_FreeSurface(image);
		image = converted;
	}
	*texture = SDL_CreateTextureFromSurface(RENDERER, image);
	SDL_FreeSurface(image);
	image = NULL;
}

void SDL_DrawImage(SDL_Renderer *renderer, SDL_Texture *texture, int x, int y)
{
	SDL_Rect position;
	position.x = x;
	position.y = y;
	SDL_QueryTexture(texture, NULL, NULL, &position.w, &position.h);
	SDL_RenderCopy(renderer, texture, NULL, &position);
}

void SDL_DrawImageScale(SDL_Renderer *renderer, SDL_Texture *texture, int x, int y, int w, int h)
{
	SDL_Rect position;
	position.x = x;
	position.y = y;
	position.w = w;
	position.h = h;
	SDL_RenderCopy(renderer, texture, NULL, &position);
}

void SDL_DrawButtonPrompt(SDL_Renderer *renderer, SDL_Texture *texture, TTF_Font *font, SDL_Color textColor, const char *text, int x, int y, int imageW, int imageH, int imageDistance, int yTextOffset)
{
	int textWidth, textHeight;
	TTF_SizeText(font, text, &textWidth, &textHeight);

	int imageX = x;
	int imageY = y - imageH;
	SDL_DrawImageScale(renderer, texture, imageX, imageY, imageW, imageH);

	int textX = x + imageDistance + imageW;
	int textY = y - (imageH * 0.85) - yTextOffset; // 0.85 was retreived from trail and error when trying to center the text with the image.
	SDL_DrawText(RENDERER, ROBOTO_20, textX, textY, textColor, text);
}

void SDL_DrawHorizonalAlignedImageText(SDL_Renderer *renderer, SDL_Texture *texture, TTF_Font *font, SDL_Color textColor, const char *text, int x, int y, int imageW, int imageH, int imageDistance, int yTextOffset)
{
	SDL_DrawButtonPrompt(renderer, texture, font, textColor, text, x, y, imageW, imageH, imageDistance, yTextOffset);
}