#include <malloc.h>
#include <stdio.h>

#include <3ds.h>
#include <citro3d.h>

#include "screen.h"
#include "lodepng.h"
#include "util.h"
#include "default_shbin.h"

GX_TRANSFER_FORMAT gpuToGxFormat[13] = {
        GX_TRANSFER_FMT_RGBA8,
        GX_TRANSFER_FMT_RGB8,
        GX_TRANSFER_FMT_RGB5A1,
        GX_TRANSFER_FMT_RGB565,
        GX_TRANSFER_FMT_RGBA4,
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8  // Unsupported
};

static bool c3dInitialized;

static bool shaderInitialized;
static DVLB_s* dvlb;
static shaderProgram_s program;

static C3D_RenderTarget* target_top;
static C3D_RenderTarget* target_bottom;
static C3D_Mtx projection_top;
static C3D_Mtx projection_bottom;

static struct {
    bool initialized;
    C3D_Tex tex;
    u32 width;
    u32 height;
    u32 pow2Width;
    u32 pow2Height;
} textures[MAX_TEXTURES];

static C3D_Tex* glyphSheets;

void screen_init() {
    if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2)) {
        util_panic("Failed to initialize the GPU.");
        return;
    }

    c3dInitialized = true;

    target_top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if(target_top == NULL) {
        util_panic("Failed to initialize the top screen target.");
        return;
    }

    u32 displayFlags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

    C3D_RenderTargetSetClear(target_top, C3D_CLEAR_ALL, 0, 0);
    C3D_RenderTargetSetOutput(target_top, GFX_TOP, GFX_LEFT, displayFlags);

    target_bottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if(target_bottom == NULL) {
        util_panic("Failed to initialize the bottom screen target.");
        return;
    }

    C3D_RenderTargetSetClear(target_bottom, C3D_CLEAR_ALL, 0, 0);
    C3D_RenderTargetSetOutput(target_bottom, GFX_BOTTOM, GFX_LEFT, displayFlags);

    dvlb = DVLB_ParseFile((u32*) default_shbin, default_shbin_len);
    if(dvlb == NULL) {
        util_panic("Failed to parse shader.");
        return;
    }

    Result progInitRes = shaderProgramInit(&program);
    if(R_FAILED(progInitRes)) {
        util_panic("Failed to initialize shader program: 0x%08lX", progInitRes);
        return;
    }

    shaderInitialized = true;

    Result progSetVshRes = shaderProgramSetVsh(&program, &dvlb->DVLE[0]);
    if(R_FAILED(progSetVshRes)) {
        util_panic("Failed to set up vertex shader: 0x%08lX", progInitRes);
        return;
    }

    C3D_BindProgram(&program);

    C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
    if(attrInfo == NULL) {
        util_panic("Failed to retrieve attribute info.");
        return;
    }

    AttrInfo_Init(attrInfo);
    AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2);

    C3D_TexEnv* env = C3D_GetTexEnv(0);
    if(env == NULL) {
        util_panic("Failed to retrieve combiner settings.");
        return;
    }

    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);

    Mtx_OrthoTilt(&projection_top, 0.0, 400.0, 240.0, 0.0, 0.0, 1.0);
    Mtx_OrthoTilt(&projection_bottom, 0.0, 320.0, 240.0, 0.0, 0.0, 1.0);

    Result fontMapRes = fontEnsureMapped();
    if(R_FAILED(fontMapRes)) {
        util_panic("Failed to map system font: 0x%08lX", fontMapRes);
        return;
    }

    TGLP_s* glyphInfo = fontGetGlyphInfo();
    glyphSheets = calloc(1, glyphInfo->nSheets * sizeof(C3D_Tex));
    for(int i = 0; i < glyphInfo->nSheets; i++) {
        C3D_Tex* tex = &glyphSheets[i];
        tex->data = fontGetGlyphSheetTex(i);
        tex->fmt = (GPU_TEXCOLOR) glyphInfo->sheetFmt;
        tex->size = glyphInfo->sheetSize;
        tex->width = glyphInfo->sheetWidth;
        tex->height = glyphInfo->sheetHeight;
        tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR) | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
    }

    screen_load_texture_file(TEXTURE_BOTTOM_SCREEN_BG, "bottom_screen_bg.png", true);
    screen_load_texture_file(TEXTURE_BOTTOM_SCREEN_TOP_BAR, "bottom_screen_top_bar.png", true);
    screen_load_texture_file(TEXTURE_BOTTOM_SCREEN_TOP_BAR_SHADOW, "bottom_screen_top_bar_shadow.png", true);
    screen_load_texture_file(TEXTURE_BOTTOM_SCREEN_BOTTOM_BAR, "bottom_screen_bottom_bar.png", true);
    screen_load_texture_file(TEXTURE_BOTTOM_SCREEN_BOTTOM_BAR_SHADOW, "bottom_screen_bottom_bar_shadow.png", true);
    screen_load_texture_file(TEXTURE_TOP_SCREEN_BG, "top_screen_bg.png", true);
    screen_load_texture_file(TEXTURE_TOP_SCREEN_TOP_BAR, "top_screen_top_bar.png", true);
    screen_load_texture_file(TEXTURE_TOP_SCREEN_TOP_BAR_SHADOW, "top_screen_top_bar_shadow.png", true);
    screen_load_texture_file(TEXTURE_TOP_SCREEN_BOTTOM_BAR, "top_screen_bottom_bar.png", true);
    screen_load_texture_file(TEXTURE_TOP_SCREEN_BOTTOM_BAR_SHADOW, "top_screen_bottom_bar_shadow.png", true);
    screen_load_texture_file(TEXTURE_LOGO, "logo.png", true);
    screen_load_texture_file(TEXTURE_SELECTION_OVERLAY, "selection_overlay.png", true);
    screen_load_texture_file(TEXTURE_BUTTON_SMALL, "button_small.png", true);
    screen_load_texture_file(TEXTURE_BUTTON_LARGE, "button_large.png", true);
    screen_load_texture_file(TEXTURE_PROGRESS_BAR_BG, "progress_bar_bg.png", true);
    screen_load_texture_file(TEXTURE_PROGRESS_BAR_CONTENT, "progress_bar_content.png", true);
    screen_load_texture_file(TEXTURE_SMDH_INFO_BOX, "smdh_info_box.png", true);
    screen_load_texture_file(TEXTURE_SMDH_INFO_BOX_SHADOW, "smdh_info_box_shadow.png", true);
    screen_load_texture_file(TEXTURE_BATTERY_CHARGING, "battery_charging.png", true);
    screen_load_texture_file(TEXTURE_BATTERY_0, "battery0.png", true);
    screen_load_texture_file(TEXTURE_BATTERY_1, "battery1.png", true);
    screen_load_texture_file(TEXTURE_BATTERY_2, "battery2.png", true);
    screen_load_texture_file(TEXTURE_BATTERY_3, "battery3.png", true);
    screen_load_texture_file(TEXTURE_BATTERY_4, "battery4.png", true);
    screen_load_texture_file(TEXTURE_BATTERY_5, "battery5.png", true);
    screen_load_texture_file(TEXTURE_WIFI_DISCONNECTED, "wifi_disconnected.png", true);
    screen_load_texture_file(TEXTURE_WIFI_0, "wifi0.png", true);
    screen_load_texture_file(TEXTURE_WIFI_1, "wifi1.png", true);
    screen_load_texture_file(TEXTURE_WIFI_2, "wifi2.png", true);
    screen_load_texture_file(TEXTURE_WIFI_3, "wifi3.png", true);
}

void screen_exit() {
    for(u32 id = 0; id < MAX_TEXTURES; id++) {
        screen_unload_texture(id);
    }

    if(glyphSheets != NULL) {
        free(glyphSheets);
        glyphSheets = NULL;
    }

    if(shaderInitialized) {
        shaderProgramFree(&program);
        shaderInitialized = false;
    }

    if(dvlb != NULL) {
        DVLB_Free(dvlb);
        dvlb = NULL;
    }

    if(target_top != NULL) {
        C3D_RenderTargetDelete(target_top);
        target_top = NULL;
    }

    if(target_bottom != NULL) {
        C3D_RenderTargetDelete(target_bottom);
        target_bottom = NULL;
    }

    if(c3dInitialized) {
        C3D_Fini();
        c3dInitialized = false;
    }
}

u32 next_pow_2(u32 i) {
    i--;
    i |= i >> 1;
    i |= i >> 2;
    i |= i >> 4;
    i |= i >> 8;
    i |= i >> 16;
    i++;

    return i;
}

void screen_load_texture(u32 id, unsigned char* image, u32 width, u32 height, bool linearFilter) {
    u32 pow2Width = next_pow_2(width);
    if(pow2Width < 64) {
        pow2Width = 64;
    }

    u32 pow2Height = next_pow_2(height);
    if(pow2Height < 64) {
        pow2Height = 64;
    }

    u8* gpuTex = linearAlloc(pow2Width * pow2Height * 4);
    if(gpuTex == NULL) {
        util_panic("Failed to allocate temporary texture buffer for id \"%i\".", id);
        return;
    }

    memset(gpuTex, 0, pow2Width * pow2Height * 4);

    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            u32 imagePos = (y * width + x) * 4;
            u32 gpuTexPos = (y * pow2Width + x) * 4;

            gpuTex[gpuTexPos + 0] = image[imagePos + 3];
            gpuTex[gpuTexPos + 1] = image[imagePos + 2];
            gpuTex[gpuTexPos + 2] = image[imagePos + 1];
            gpuTex[gpuTexPos + 3] = image[imagePos + 0];
        }
    }

    textures[id].initialized = true;
    textures[id].width = width;
    textures[id].height = height;
    textures[id].pow2Width = pow2Width;
    textures[id].pow2Height = pow2Height;

    if(!C3D_TexInit(&textures[id].tex, (int) pow2Width, (int) pow2Height, GPU_RGBA8)) {
        util_panic("Failed to initialize texture for id \"%i\".", id);
        return;
    }

    C3D_TexSetFilter(&textures[id].tex, linearFilter ? GPU_LINEAR : GPU_NEAREST, GPU_NEAREST);

    Result flushRes = GSPGPU_FlushDataCache(gpuTex, pow2Width * pow2Height * 4);
    if(R_FAILED(flushRes)) {
        util_panic("Failed to flush texture buffer for id \"%i\": 0x%08lX", id, flushRes);
        return;
    }

    Result transferRes = GX_DisplayTransfer((u32*) gpuTex, GX_BUFFER_DIM(pow2Width, pow2Height), (u32*) textures[id].tex.data, GX_BUFFER_DIM(pow2Width, pow2Height), GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    if(R_FAILED(transferRes)) {
        util_panic("Failed to tile texture data for id \"%s\": 0x%08lX", id, transferRes);
        return;
    }

    gspWaitForPPF();

    linearFree(gpuTex);
}

u32 screen_load_texture_auto(unsigned char* image, u32 width, u32 height, bool linearFilter) {
    int id = -1;
    for(int i = TEXTURE_AUTO_START; i < MAX_TEXTURES; i++) {
        if(!textures[i].initialized) {
            id = i;
            break;
        }
    }

    if(id == -1) {
        util_panic("Attempted to load auto texture without free textures.");
        return 0;
    }

    screen_load_texture((u32) id, image, width, height, linearFilter);
    return (u32) id;
}

void screen_load_texture_file(u32 id, const char* path, bool linearFilter) {
    if(id >= MAX_TEXTURES) {
        util_panic("Attempted to load path \"%s\" to invalid texture ID \"%lu\".", path, id);
        return;
    }

    u32 realPathSize = strlen(path) + 16;
    char realPath[realPathSize];
    snprintf(realPath, realPathSize, "sdmc:/fbitheme/%s", path);
    FILE* fd = fopen(realPath, "rb");
    if(fd != NULL) {
        fclose(fd);
    } else {
        snprintf(realPath, realPathSize, "romfs:/%s", path);
    }

    unsigned char* image;
    unsigned width;
    unsigned height;
    unsigned pngErr = lodepng_decode32_file(&image, &width, &height, realPath);
    if(pngErr != 0) {
        util_panic("Failed to load PNG file \"%s\": %u", realPath, pngErr);
        return;
    }

    screen_load_texture(id, image, width, height, linearFilter);
    free(image);
    return;
}

u32 screen_load_texture_file_auto(const char* path, bool linearFilter) {
    int id = -1;
    for(int i = TEXTURE_AUTO_START; i < MAX_TEXTURES; i++) {
        if(!textures[i].initialized) {
            id = i;
            break;
        }
    }

    if(id == -1) {
        util_panic("Attempted to load auto texture from path \"%s\" without free textures.", path);
        return 0;
    }

    screen_load_texture_file((u32) id, path, linearFilter);
    return (u32) id;
}

static u32 tiled_texture_index(u32 x, u32 y, u32 w, u32 h) {
    return (((y >> 3) * (w >> 3) + (x >> 3)) << 6) + ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
}

void screen_load_texture_tiled(u32 id, void* tiledData, u32 size, u32 width, u32 height, GPU_TEXCOLOR format, bool linearFilter) {
    if(id >= MAX_TEXTURES) {
        util_panic("Attempted to load tiled data to invalid texture ID \"%lu\".", id);
        return;
    }

    u32 pow2Width = next_pow_2(width);
    if(pow2Width < 64) {
        pow2Width = 64;
    }

    u32 pow2Height = next_pow_2(height);
    if(pow2Height < 64) {
        pow2Height = 64;
    }

    u32 pixelSize = size / width / height;

    u8* pow2Tex = linearAlloc(pow2Width * pow2Height * pixelSize);
    if(pow2Tex == NULL) {
        util_panic("Failed to allocate temporary texture buffer for tiled data.");
        return;
    }

    memset(pow2Tex, 0, pow2Width * pow2Height * pixelSize);

    for(u32 x = 0; x < width; x++) {
        for(u32 y = 0; y < height; y++) {
            u32 tiledDataPos = tiled_texture_index(x, y, width, height) * pixelSize;
            u32 pow2TexPos = (y * pow2Width + x) * pixelSize;

            for(u32 i = 0; i < pixelSize; i++) {
                pow2Tex[pow2TexPos + i] = ((u8*) tiledData)[tiledDataPos + i];
            }
        }
    }

    textures[id].initialized = true;
    textures[id].width = width;
    textures[id].height = height;
    textures[id].pow2Width = pow2Width;
    textures[id].pow2Height = pow2Height;

    if(!C3D_TexInit(&textures[id].tex, (int) pow2Width, (int) pow2Height, format)) {
        util_panic("Failed to initialize texture for tiled data.");
        return;
    }

    C3D_TexSetFilter(&textures[id].tex, linearFilter ? GPU_LINEAR : GPU_NEAREST, GPU_NEAREST);
    C3D_TexUpload(&textures[id].tex, pow2Tex);

    Result flushRes = GSPGPU_FlushDataCache(pow2Tex, pow2Width * pow2Height * 4);
    if(R_FAILED(flushRes)) {
        util_panic("Failed to flush texture buffer for tiled data: 0x%08lX", flushRes);
        return;
    }

    Result transferRes = GX_DisplayTransfer((u32*) pow2Tex, GX_BUFFER_DIM(pow2Width, pow2Height), (u32*) textures[id].tex.data, GX_BUFFER_DIM(pow2Width, pow2Height), GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT((u32) gpuToGxFormat[format]) | GX_TRANSFER_OUT_FORMAT((u32) gpuToGxFormat[format]) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    if(R_FAILED(transferRes)) {
        util_panic("Failed to tile texture data for tiled data: 0x%08lX", transferRes);
        return;
    }

    gspWaitForPPF();

    linearFree(pow2Tex);
}

u32 screen_load_texture_tiled_auto(void* tiledData, u32 size, u32 width, u32 height, GPU_TEXCOLOR format, bool linearFilter) {
    int id = -1;
    for(int i = TEXTURE_AUTO_START; i < MAX_TEXTURES; i++) {
        if(!textures[i].initialized) {
            id = i;
            break;
        }
    }

    if(id == -1) {
        util_panic("Attempted to load auto texture from tiled data without free textures.");
        return 0;
    }

    screen_load_texture_tiled((u32) id, tiledData, size, width, height, format, linearFilter);
    return (u32) id;
}

void screen_unload_texture(u32 id) {
    if(id >= MAX_TEXTURES) {
        util_panic("Attempted to unload invalid texture ID \"%lu\".", id);
        return;
    }

    if(textures[id].initialized) {
        C3D_TexDelete(&textures[id].tex);

        textures[id].initialized = false;
        textures[id].width = 0;
        textures[id].height = 0;
        textures[id].pow2Width = 0;
        textures[id].pow2Height = 0;
    }
}

void screen_get_texture_size(u32* width, u32* height, u32 id) {
    if(id >= MAX_TEXTURES || !textures[id].initialized) {
        util_panic("Attempted to get size of invalid texture ID \"%lu\".", id);
        return;
    }

    if(width) {
        *width = textures[id].width;
    }

    if(height) {
        *height = textures[id].height;
    }
}

void screen_begin_frame() {
    if(!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) {
        util_panic("Failed to begin frame.");
        return;
    }
}

void screen_end_frame() {
    C3D_FrameEnd(0);
}

void screen_select(gfxScreen_t screen) {
    if(!C3D_FrameDrawOn(screen == GFX_TOP ? target_top : target_bottom)) {
        util_panic("Failed to select render target.");
        return;
    }

    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, shaderInstanceGetUniformLocation(program.vertexShader, "projection"), screen == GFX_TOP ? &projection_top : &projection_bottom);
}

void draw_quad(float x1, float y1, float x2, float y2, float tx1, float ty1, float tx2, float ty2) {
    C3D_ImmDrawBegin(GPU_TRIANGLES);

    C3D_ImmSendAttrib(x1, y1, 0.5f, 0.0f);
    C3D_ImmSendAttrib(tx1, ty1, 0.0f, 0.0f);

    C3D_ImmSendAttrib(x2, y2, 0.5f, 0.0f);
    C3D_ImmSendAttrib(tx2, ty2, 0.0f, 0.0f);

    C3D_ImmSendAttrib(x2, y1, 0.5f, 0.0f);
    C3D_ImmSendAttrib(tx2, ty1, 0.0f, 0.0f);

    C3D_ImmSendAttrib(x1, y1, 0.5f, 0.0f);
    C3D_ImmSendAttrib(tx1, ty1, 0.0f, 0.0f);

    C3D_ImmSendAttrib(x1, y2, 0.5f, 0.0f);
    C3D_ImmSendAttrib(tx1, ty2, 0.0f, 0.0f);

    C3D_ImmSendAttrib(x2, y2, 0.5f, 0.0f);
    C3D_ImmSendAttrib(tx2, ty2, 0.0f, 0.0f);

    C3D_ImmDrawEnd();
}

void screen_draw_texture(u32 id, float x, float y, float width, float height) {
    if(id >= MAX_TEXTURES || !textures[id].initialized) {
        util_panic("Attempted to draw invalid texture ID \"%lu\".", id);
        return;
    }

    C3D_TexBind(0, &textures[id].tex);
    draw_quad(x, y, x + width, y + height, 0, 0, (float) textures[id].width / (float) textures[id].pow2Width, (float) textures[id].height / (float) textures[id].pow2Height);
}

void screen_draw_texture_crop(u32 id, float x, float y, float width, float height) {
    if(id >= MAX_TEXTURES || !textures[id].initialized) {
        util_panic("Attempted to draw invalid texture ID \"%lu\".", id);
        return;
    }

    C3D_TexBind(0, &textures[id].tex);
    draw_quad(x, y, x + width, y + height, 0, 0, width / (float) textures[id].pow2Width, height / (float) textures[id].pow2Height);
}

static void screen_get_string_size_internal(float* width, float* height, const char* text, float scaleX, float scaleY, bool oneLine) {
    float w = 0;
    float h = scaleY * fontGetInfo()->lineFeed;
    float lineWidth = 0;

    const uint8_t* p = (const uint8_t*) text;
    uint32_t code = 0;
    ssize_t units = -1;
    while(*p && (units = decode_utf8(&code, p)) != -1 && code > 0) {
        p += units;

        if(code == '\n') {
            if(*p) {
                if(lineWidth > w) {
                    w = lineWidth;
                }

                lineWidth = 0;

                if(oneLine) {
                    break;
                }

                h += scaleY * fontGetInfo()->lineFeed;
            }
        } else {
            lineWidth += scaleX * fontGetCharWidthInfo(fontGlyphIndexFromCodePoint(code))->charWidth;
        }
    }

    if(width) {
        *width = lineWidth > w ? lineWidth : w;
    }

    if(height) {
        *height = h;
    }
}

void screen_get_string_size(float* width, float* height, const char* text, float scaleX, float scaleY) {
    screen_get_string_size_internal(width, height, text, scaleX, scaleY, false);
}

void screen_draw_string(const char* text, float x, float y, float scaleX, float scaleY, u32 rgba, bool baseline) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    if(env == NULL) {
        util_panic("Failed to retrieve combiner settings.");
        return;
    }

    C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, 0, 0);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT, 0);
    C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
    C3D_TexEnvColor(env, rgba);

    float stringWidth;
    screen_get_string_size_internal(&stringWidth, NULL, text, scaleX, scaleY, false);

    float lineWidth;
    screen_get_string_size_internal(&lineWidth, NULL, text, scaleX, scaleY, true);

    float currX = x + (stringWidth - lineWidth) / 2;

    u32 flags = GLYPH_POS_CALC_VTXCOORD | (baseline ? GLYPH_POS_AT_BASELINE : 0);
    int lastSheet = -1;

    const uint8_t* p = (const uint8_t*) text;
    uint32_t code = 0;
    ssize_t units = -1;
    while(*p && (units = decode_utf8(&code, p)) != -1 && code > 0) {
        p += units;

        if(code == '\n') {
            if(*p) {
                screen_get_string_size_internal(&lineWidth, NULL, (const char*) p, scaleX, scaleY, true);
                currX = x + (stringWidth - lineWidth) / 2;
                y += scaleY * fontGetInfo()->lineFeed;
            }
        } else {
            fontGlyphPos_s data;
            fontCalcGlyphPos(&data, fontGlyphIndexFromCodePoint(code), flags, scaleX, scaleY);

            if(data.sheetIndex != lastSheet) {
                lastSheet = data.sheetIndex;
                C3D_TexBind(0, &glyphSheets[lastSheet]);
            }

            draw_quad(currX + data.vtxcoord.left, y + data.vtxcoord.top, currX + data.vtxcoord.right, y + data.vtxcoord.bottom, data.texcoord.left, data.texcoord.top, data.texcoord.right, data.texcoord.bottom);

            currX += data.xAdvance;
        }
    }

    env = C3D_GetTexEnv(0);
    if(env == NULL) {
        util_panic("Failed to retrieve combiner settings.");
        return;
    }

    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}
