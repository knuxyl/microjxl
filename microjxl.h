// microjxl: a fork of the J40 JPEG XL decoder
//
// microjxl is a fork of J40, an independent, self-contained JPEG XL decoder written by
// Kang Seonghoon (version 2270, 2022-09, Public Domain / CC0):
//     https://github.com/lifthrasiir/j40
//
// This fork continues where J40 left off: the original library implemented a large subset of
// JPEG XL (ISO/IEC 18181), but left out or stubbed a number of features. microjxl aims to
// implement ALL features described in the JPEG XL specification (parts 1-4) that J40 was
// missing, including animation, preview frames, reference frames, frame blending/canvas
// compositing, patches, noise and splines. (The public pixel accessors currently cover RGBA
// 8-bit; more output sample formats are planned.)
//
// It remains a decoder for the JPEG XL (ISO/IEC 18181) image format and a fully independent
// reimplementation compared to the reference implementation, libjxl. As in J40, all public
// identifiers use the `microjxl_`/`MICROJXL_` prefix (renamed from J40's `j40_`/`J40_`).
//
// The following is a simple but complete converter from JPEG XL to Portable Arbitrary Map format:
//
// The following is a simple but complete converter from JPEG XL to Portable Arbitrary Map format:
//
/* -------------------------------------------------------------------------------- //
#define MICROJXL_IMPLEMENTATION // only a SINGLE file should have this
#include "microjxl.h" // you also need to define a macro for experimental versions; follow the error.
#include <stdio.h>
#include <stdarg.h> // for va_*

static int oops(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) return oops("Usage: %s input.jxl output.pam\n", argv[0]);

    FILE *out = fopen(argv[2], "wb");
    if (!out) return oops("Error: Cannot open an output file.\n");

    microjxl_image image;
    microjxl_from_file(&image, argv[1]); // or: microjxl_from_memory(&image, buf, bufsize, freefunc);
    microjxl_output_format(&image, MICROJXL_RGBA, MICROJXL_U8X4);

    // JPEG XL supports animation, so `microjxl_next_frame` calls can be called multiple times
    while (microjxl_next_frame(&image)) {
        microjxl_frame frame = microjxl_current_frame(&image);
        microjxl_pixels_u8x4 pixels = microjxl_frame_pixels_u8x4(&frame, MICROJXL_RGBA);
        fprintf(out,
            "P7\n"
            "WIDTH %d\n"
            "HEIGHT %d\n"
            "DEPTH 4\n"
            "MAXVAL 255\n"
            "TUPLTYPE RGB_ALPHA\n"
            "ENDHDR\n",
            pixels.width, pixels.height);
        for (int y = 0; y < pixels.height; ++y) {
            fwrite(microjxl_row_u8x4(pixels, y), 4, pixels.width, out);
        }
    }

    // microjxl stops once the first error is encountered; its error can be checked at the very end
    if (microjxl_error(&image)) return oops("Error: %s\n", microjxl_error_string(&image));
    if (ferror(out)) return oops("Error: Cannot fully write to the output file.\n");

    microjxl_free(&image); // also frees all memory associated to microjxl_frame etc.
    fclose(out);
    return 0;
}
// -------------------------------------------------------------------------------- */

////////////////////////////////////////////////////////////////////////////////
// preamble (only reachable via the user `#include`)

// controls whether each `#if`-`#endif` section in this file should be included or not.
// there are multiple purposes of this macro:
// - `MICROJXL__RECURSING` is always defined after the first ever `#include`, so that:
//   - the preamble will precede every other code in the typical usage, and
//   - the preamble won't be included twice.
// - `MICROJXL__RECURSING` is either 0 (public) or -1 (internal) depending on the logical visibility,
//   so that the preamble can choose whether to include the internal code or not.
// - larger values (>= 100) are used to repeat a specific section of code with
//   slightly different parameters, i.e. templated code.
// - one value (currently 9999) is reserved and used to ignore subsequent top-level `#include`s.
#ifndef MICROJXL__RECURSING

#define MICROJXL_VERSION 2270 // (fractional gregorian year - 2000) * 100, with a liberal rounding

#ifndef MICROJXL_CONFIRM_THAT_THIS_IS_EXPERIMENTAL_AND_POTENTIALLY_UNSAFE
#error "Please #define MICROJXL_CONFIRM_THAT_THIS_IS_EXPERIMENTAL_AND_POTENTIALLY_UNSAFE to use microjxl. Proceed at your own risk."
#endif

//#define MICROJXL_DEBUG

#ifndef MICROJXL_FILENAME // should be provided if this file has a different name than `microjxl.h`
#define MICROJXL_FILENAME "microjxl.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef MICROJXL_IMPLEMENTATION
	#define MICROJXL__IMPLEMENTATION_INCLUDED
	#include <string.h>
	#include <math.h>
	#include <limits.h>
	#include <float.h>
	#include <errno.h>
	#include <stdio.h>
	#ifdef MICROJXL_DEBUG
		#include <assert.h>
		static int64_t microjxl__debug_wp_stream_id = -1;
	#endif
	#ifndef MICROJXL__EXPOSE_INTERNALS
		#define MICROJXL__EXPOSE_INTERNALS
	#endif
#endif

#ifdef MICROJXL__EXPOSE_INTERNALS
	#define MICROJXL__RECURSING (-1)
#else
	#define MICROJXL__RECURSING 0
#endif

// we don't care about secure CRT, which is only marginally safe and not even compatible with C11
#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable: 4996)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#endif // !defined MICROJXL__RECURSING

#if MICROJXL__RECURSING == 9999 // enabled only when the header file is included the second time or more
	#if !defined MICROJXL__IMPLEMENTATION_INCLUDED && defined MICROJXL_IMPLEMENTATION
		#error "microjxl is included with MICROJXL_IMPLEMENTATION defined, but it was already included without it so it would have been ignored!"
	#endif
#endif

////////////////////////////////////////////////////////////////////////////////
// public platform macros

#if MICROJXL__RECURSING <= 0

// just in case:
#if CHAR_BIT != 8 // in fact, pretty much every file processing wouldn't make sense if CHAR_BIT > 8
	#error "microjxl requires CHAR_BIT == 8"
#endif

#ifndef MICROJXL_STATIC_ASSERT
	#if __STDC_VERSION__ >= 199901L
		#define MICROJXL_STATIC_ASSERT(cond, msg) _Static_assert(cond, #msg)
	#else
		#define MICROJXL_STATIC_ASSERT(cond, msg) typedef char microjxl__##msg[(cond) ? 1 : -1]
	#endif
#endif // !defined MICROJXL_STATIC_ASSERT

// just in case again, because it is still possible for them to have padding bits (that we needn't):
MICROJXL_STATIC_ASSERT(sizeof(uint8_t) == 1, uint8_t_should_have_no_padding_bits);
MICROJXL_STATIC_ASSERT(sizeof(uint16_t) == 2, uint16_t_should_have_no_padding_bits);
MICROJXL_STATIC_ASSERT(sizeof(uint32_t) == 4, uint32_t_should_have_no_padding_bits);
MICROJXL_STATIC_ASSERT(sizeof(uint64_t) == 8, uint64_t_should_have_no_padding_bits);

#ifndef MICROJXL_API
	#define MICROJXL_API // TODO
#endif

#endif // MICROJXL__RECURSING <= 0

////////////////////////////////////////////////////////////////////////////////
// public API

#if MICROJXL__RECURSING <= 0

// an internal error type. non-zero indicates a different error condition.
// user callbacks can also emit error codes, which should not exceed `MICROJXL_MIN_RESERVED_ERR`.
// it can be interpreted as a four-letter code, but such encoding is not guaranteed.
typedef uint32_t microjxl_err;
#define MICROJXL_MIN_RESERVED_ERR (microjxl_err) (1 << 24) // anything below this can be used freely

typedef struct {
	// either MICROJXL__IMAGE_MAGIC, (MICROJXL__IMAGE_ERR_MAGIC ^ origin) or (MICROJXL__IMAGE_OPEN_ERR_MAGIC ^ origin)
	uint32_t magic;
	union {
		struct microjxl__inner *inner; // if magic == MICROJXL__IMAGE_MAGIC
		microjxl_err err; // if magic == MICROJXL__IMAGE_ERR_MAGIC
		int saved_errno; // if magic == MICROJXL__IMAGE_OPEN_ERR_MAGIC (err is assumed to be `open`)
	} u;
} microjxl_image;

typedef struct {
	uint32_t magic; // should be MICROJXL__FRAME_MAGIC or MICROJXL__FRAME_ERR_MAGIC
	uint32_t reserved;
	struct microjxl__inner *inner;
} microjxl_frame;

typedef void (*microjxl_memory_free_func)(void *data);

// pixel formats
#define MICROJXL_U8                  0x0f0f // single-channel (planar) unsigned 8-bit
//rsvd: MICROJXL_U32                 0x0f1b
//rsvd: MICROJXL_U64                 0x0f1d
#define MICROJXL_U16                 0x0f17 // single-channel (planar) unsigned 16-bit
#define MICROJXL_F32                 0x0f1e // single-channel (planar) float, 0..1
//rsvd: MICROJXL_U8X3                0x0f27
//rsvd: MICROJXL_U16X3               0x0f2b
//rsvd: MICROJXL_U32X3               0x0f2d
//rsvd: MICROJXL_F32X3               0x0f2e
#define MICROJXL_U8X4                0x0f33
#define MICROJXL_U16X4               0x0f35
//rsvd: MICROJXL_U32X4               0x0f36
#define MICROJXL_F32X4               0x0f39

// color types
#define MICROJXL_RED                 0x170f
#define MICROJXL_GREEN               0x1717
#define MICROJXL_BLUE                0x171b
//rsvd: MICROJXL_LUMI                0x171d
#define MICROJXL_ALPHA               0x171e
//rsvd: MICROJXL_CYAN                0x1727
//rsvd: MICROJXL_YELLOW              0x172b
//rsvd: MICROJXL_MAGENTA             0x172d
//rsvd: MICROJXL_BLACK               0x172e
//rsvd: MICROJXL_JPEG_Y              0x1733
//rsvd: MICROJXL_JPEG_CB             0x1735
//rsvd: MICROJXL_JPEG_CR             0x1736
//rsvd: MICROJXL_OPSIN_X             0x1739
//rsvd: MICROJXL_OPSIN_Y             0x173a
//rsvd: MICROJXL_OPSIN_B             0x173c
//rsvd: MICROJXL_RED_BEFORE_CT       0x1747
//rsvd: MICROJXL_GREEN_BEFORE_CT     0x174b
//rsvd: MICROJXL_BLUE_BEFORE_CT      0x174d
//rsvd: MICROJXL_RGB                 0x174e
//rsvd: MICROJXL_BGR                 0x1753
#define MICROJXL_RGBA                0x1755
//rsvd: MICROJXL_ARGB                0x1756//rsvd: MICROJXL_BGRA               0x1759
//rsvd: MICROJXL_ABGR               0x175a

// extra channel types (values match the spec's ExtraChannel enum)
#define MICROJXL_EC_ALPHA            0
#define MICROJXL_EC_DEPTH            1
#define MICROJXL_EC_SPOT             2
#define MICROJXL_EC_SELECTION        3
#define MICROJXL_EC_BLACK            4
#define MICROJXL_EC_CFA              5
#define MICROJXL_EC_THERMAL          6
#define MICROJXL_EC_NON_OPTIONAL     15
#define MICROJXL_EC_OPTIONAL         16

MICROJXL_API microjxl_err microjxl_error(const microjxl_image *image);
MICROJXL_API const char *microjxl_error_string(const microjxl_image *image);

MICROJXL_API microjxl_err microjxl_from_memory(microjxl_image *image, void *buf, size_t size, microjxl_memory_free_func freefunc);
MICROJXL_API microjxl_err microjxl_from_file(microjxl_image *image, const char *path);

MICROJXL_API microjxl_err microjxl_output_format(microjxl_image *image, int32_t channel, int32_t format);

// Returns the ICC profile of the image: the embedded one when the stream
// carries it, or a generated profile built from the enum ColourEncoding
// (libjxl MaybeCreateProfile port) when it does not. Valid after the first
// successful `microjxl_next_frame`. The pointer is owned by the image and
// valid until `microjxl_free`.
MICROJXL_API const void *microjxl_icc_profile(const microjxl_image *image, size_t *size);

/* Returns the reconstructed JPEG byte stream for a file carrying a jbrd box
 * (Part 2 §9.10 / Annex "JPEG reconstruction"), or NULL when the image has
 * no reconstruction data (and on error). The returned buffer is owned by the
 * image and stays valid until microjxl_free; `*size` receives its length.
 * NULL with a non-zero *size means the image HAS reconstruction data but
 * decoding it failed (the error state of the image is set). */
MICROJXL_API const void *microjxl_jpeg_reconstruction(const microjxl_image *image, size_t *size);

// Returns the ICC profile of the image's *output* colour encoding (libjxl
// `JXL_COLOR_PROFILE_TARGET_DATA`). This differs from `microjxl_icc_profile`
// (`TARGET_ORIGINAL`) whenever the two encodings differ: for an XYB-encoded
// image whose original encoding is carried by an ICC and cannot be output
// directly, libjxl uses LinearSRGB(is_gray) (libjxl dec_xyb.cc
// OutputEncodingInfo::SetFromMetadata), so this returns a generated linear
// sRGB/gray profile instead of the embedded one. For non-XYB streams the
// original encoding is used, i.e. the embedded profile when present.
// Valid after the first successful `microjxl_next_frame`; owned by the image
// and valid until `microjxl_free`.
MICROJXL_API const void *microjxl_output_icc_profile(const microjxl_image *image, size_t *size);

// Enables or disables coalescing (default: enabled, libjxl-compatible).
// With coalescing disabled, animation/crop frames are handed out as their
// individual layers without canvas compositing or blending: each call of
// `microjxl_next_frame` yields the next REGULAR/SKIPPROG frame rendered at
// its own size and position (use `microjxl_frame_*` accessors for the
// geometry), including zero-duration non-last frames that would be merged
// into the canvas when coalescing. Reference-only and LF frames are never
// exposed in either mode.
MICROJXL_API microjxl_err microjxl_set_coalescing(microjxl_image *image, int coalescing);

// Enables or disables spot-colour rendering (default: enabled, like libjxl).
// When disabled, spot-colour extra channels are left out of the colour
// output (libjxl's --norender_spotcolors); they remain accessible as extra
// channels. Must be called before the first `microjxl_next_frame`.

MICROJXL_API int microjxl_next_frame(microjxl_image *image);
MICROJXL_API microjxl_frame microjxl_current_frame(microjxl_image *image);

// Geometry/metadata of the current frame (valid once the frame has been
// handed out by `microjxl_next_frame`). In coalesced mode width/height are
// the canvas dimensions and x0/y0 are 0; in non-coalesced mode they are the
// frame's own geometry. `duration` is in animation ticks (0 for stills).

#define MICROJXL__DEFINE_PIXELS(type, suffix) \
	typedef struct { \
		int32_t width, height; \
		int32_t stride_bytes; \
		const void *data; \
	} microjxl_pixels_##suffix; \
	MICROJXL_API microjxl_pixels_##suffix microjxl_frame_pixels_##suffix(const microjxl_frame *frame, int32_t channel); \
	MICROJXL_API const type *microjxl_row_##suffix(microjxl_pixels_##suffix pixels, int32_t y)

typedef uint8_t /*microjxl_u8x3[3],*/ microjxl_u8x4[4];
typedef uint16_t /*microjxl_u16x3[3],*/ microjxl_u16x4[4];
//typedef uint32_t microjxl_u32x3[3], microjxl_u32x4[4];
typedef float /*microjxl_f32x3[3],*/ microjxl_f32x4[4]; // RGBA, 0..1 per channel

MICROJXL__DEFINE_PIXELS(uint8_t, u8);         // microjxl_pixels_u8, microjxl_frame_pixels_u8, microjxl_row_u8
MICROJXL__DEFINE_PIXELS(uint16_t, u16);       // microjxl_pixels_u16, microjxl_frame_pixels_u16, microjxl_row_u16
//MICROJXL__DEFINE_PIXELS(uint32_t, u32);    // microjxl_pixels_u32, microjxl_frame_pixels_u32, microjxl_row_u32
//MICROJXL__DEFINE_PIXELS(uint64_t, u64);    // microjxl_pixels_u64, microjxl_frame_pixels_u64, microjxl_row_u64
MICROJXL__DEFINE_PIXELS(float, f32);          // microjxl_pixels_f32, microjxl_frame_pixels_f32, microjxl_row_f32
//MICROJXL__DEFINE_PIXELS(microjxl_u8x3, u8x3);   // microjxl_pixels_u8x3, microjxl_frame_pixels_u8x3, microjxl_row_u8x3
//MICROJXL__DEFINE_PIXELS(microjxl_u16x3, u16x3); // microjxl_pixels_u16x3, microjxl_frame_pixels_u16x3, microjxl_row_u16x3
//MICROJXL__DEFINE_PIXELS(microjxl_u32x3, u32x3); // microjxl_pixels_u32x3, microjxl_frame_pixels_u32x3, microjxl_row_u32x3
//MICROJXL__DEFINE_PIXELS(microjxl_f32x3, f32x3); // microjxl_pixels_f32x3, microjxl_frame_pixels_f32x3, microjxl_row_f32x3
MICROJXL__DEFINE_PIXELS(microjxl_u8x4, u8x4);     // microjxl_pixels_u8x4, microjxl_frame_pixels_u8x4, microjxl_row_u8x4
MICROJXL__DEFINE_PIXELS(microjxl_u16x4, u16x4);   // microjxl_pixels_u16x4, microjxl_frame_pixels_u16x4, microjxl_row_u16x4
//MICROJXL__DEFINE_PIXELS(microjxl_u32x4, u32x4); // microjxl_pixels_u32x4, microjxl_frame_pixels_u32x4, microjxl_row_u32x4
MICROJXL__DEFINE_PIXELS(microjxl_f32x4, f32x4);   // microjxl_pixels_f32x4, microjxl_frame_pixels_f32x4, microjxl_row_f32x4

MICROJXL_API void microjxl_free(microjxl_image *image);

MICROJXL_API int32_t microjxl_frame_x0(const microjxl_frame *frame);
MICROJXL_API int32_t microjxl_frame_y0(const microjxl_frame *frame);
MICROJXL_API int32_t microjxl_frame_width(const microjxl_frame *frame);
MICROJXL_API int32_t microjxl_frame_height(const microjxl_frame *frame);
MICROJXL_API int64_t microjxl_frame_duration(const microjxl_frame *frame);
MICROJXL_API int microjxl_frame_is_last(const microjxl_frame *frame);

// Extra-channel/metadata access (valid after the frame is handed out).
// Extra channels are indexed 0..microjxl_num_extra_channels()-1 and, in
// coalesced mode, expose the canvas-composited planes (canvas geometry);
// raw sample domain 0..(2^bits_per_sample - 1) as floats. Returns an empty
// plane when the image has no such channel.
MICROJXL_API int32_t microjxl_num_extra_channels(const microjxl_image *image);
MICROJXL_API int32_t microjxl_extra_channel_type(const microjxl_image *image, int32_t ec);
MICROJXL_API int32_t microjxl_extra_channel_bpp(const microjxl_image *image, int32_t ec);
MICROJXL_API int32_t microjxl_extra_channel_exp_bits(const microjxl_image *image, int32_t ec);
MICROJXL_API microjxl_pixels_f32 microjxl_frame_extra_channel_f32(const microjxl_frame *frame, int32_t ec);

// Animation metadata (0/defaults for stills): ticks per second and the
// loop count (0 = infinite).
MICROJXL_API int32_t microjxl_animation_tps_num(const microjxl_image *image);
MICROJXL_API int32_t microjxl_animation_tps_denom(const microjxl_image *image);
MICROJXL_API int64_t microjxl_animation_nloops(const microjxl_image *image);

// Image-level metadata (libjxl/18181-3 conformance JSON fields).
MICROJXL_API float microjxl_intensity_target(const microjxl_image *image);
MICROJXL_API float microjxl_min_nits(const microjxl_image *image);
MICROJXL_API float microjxl_linear_below(const microjxl_image *image);
MICROJXL_API float microjxl_relative_to_max_display(const microjxl_image *image);
MICROJXL_API int32_t microjxl_bits_per_sample(const microjxl_image *image);
MICROJXL_API int32_t microjxl_exponent_bits_per_sample(const microjxl_image *image);
MICROJXL_API int32_t microjxl_xsize(const microjxl_image *image);
MICROJXL_API int32_t microjxl_ysize(const microjxl_image *image);

// Image orientation (1..8, EXIF convention; 1 = identity). Values 5..8 are
// transposing: the display-domain image has swapped width/height, and pixels
// must be reshaped accordingly before display (libjxl applies the undo at the
// output stage; the render itself stays in the stored domain).
MICROJXL_API int32_t microjxl_orientation(const microjxl_image *image);
// Nonzero when the colour encoding is single-channel (greyscale): the decoded
// image has one colour channel + optional alpha (libjxl num_channels 1/2).
MICROJXL_API int32_t microjxl_is_grayscale(const microjxl_image *image);

// Name of the current frame (NUL-terminated, owned by the image; the name
// length is decoded per frame). Returns "" when the frame has none.
MICROJXL_API const char *microjxl_frame_name(const microjxl_frame *frame);

#endif // MICROJXL__RECURSING <= 0

////////////////////////////////////////////////////////////////////////////////
//////////////////////// internal code starts from here ////////////////////////
////////////////////////////////////////////////////////////////////////////////

#if MICROJXL__RECURSING < 0

// comment convention:
// "SPEC" comments are used for incorrect, ambiguous or misleading specification issues.
// "TODO spec" comments are roughly same, but not yet fully confirmed & reported.

////////////////////////////////////////////////////////////////////////////////
// private platform macros

#ifdef __has_attribute // since GCC 5.0.0 and clang 2.9.0
	#if __has_attribute(always_inline)
		#define MICROJXL__HAS_ALWAYS_INLINE_ATTR 1
	#endif
	#if __has_attribute(warn_unused_result)
		#define MICROJXL__HAS_WARN_UNUSED_RESULT_ATTR 1
	#endif
#endif

#ifdef __has_builtin // since GCC 10.0.0 and clang 1.0.0 (which thus requires no version check)
	#if __has_builtin(__builtin_expect)
		#define MICROJXL__HAS_BUILTIN_EXPECT 1
	#endif
	#if __has_builtin(__builtin_add_overflow)
		#define MICROJXL__HAS_BUILTIN_ADD_OVERFLOW 1
	#endif
	#if __has_builtin(__builtin_sub_overflow)
		#define MICROJXL__HAS_BUILTIN_SUB_OVERFLOW 1
	#endif
	#if __has_builtin(__builtin_mul_overflow)
		#define MICROJXL__HAS_BUILTIN_MUL_OVERFLOW 1
	#endif
	#if __has_builtin(__builtin_unreachable)
		#define MICROJXL__HAS_BUILTIN_UNREACHABLE 1
	#endif
	#if __has_builtin(__builtin_assume_aligned)
		#define MICROJXL__HAS_BUILTIN_ASSUME_ALIGNED 1
	#endif
#endif

// clang (among many others) fakes GCC version by default, but we handle clang separately
#if defined __GNUC__ && !defined __clang__
	#define MICROJXL__GCC_VER (__GNUC__ * 0x10000 + __GNUC_MINOR__ * 0x100 + __GNUC_PATCHLEVEL__)
#else
	#define MICROJXL__GCC_VER 0
#endif

#ifdef __clang__
	#define MICROJXL__CLANG_VER (__clang_major__ * 0x10000 + __clang_minor__ * 0x100 + __clang_patchlevel__)
#else
	#define MICROJXL__CLANG_VER 0
#endif

#ifndef MICROJXL_STATIC
	#define MICROJXL_STATIC static
#endif

#ifndef MICROJXL_INLINE
	#define MICROJXL_INLINE MICROJXL_STATIC inline
#endif

#ifndef MICROJXL_ALWAYS_INLINE
	#if MICROJXL__HAS_ALWAYS_INLINE_ATTR || MICROJXL__GCC_VER >= 0x30100 || MICROJXL__CLANG_VER >= 0x10000
		#define MICROJXL_ALWAYS_INLINE __attribute__((always_inline)) MICROJXL_INLINE
	#elif defined _MSC_VER
		#define MICROJXL_ALWAYS_INLINE __forceinline
	#else
		#define MICROJXL_ALWAYS_INLINE MICROJXL_INLINE
	#endif
#endif // !defined MICROJXL_ALWAYS_INLINE

#ifndef MICROJXL_RESTRICT
	#if __STDC_VERSION__ >= 199901L
		#define MICROJXL_RESTRICT restrict
	#elif defined __GNUC__ || __MSC_VER >= 1900 // since pretty much every GCC/Clang and VS 2015
		#define MICROJXL_RESTRICT __restrict
	#else
		#define MICROJXL_RESTRICT
	#endif
#endif // !defined MICROJXL_RESTRICT

// most structs in microjxl are designed to be zero-initialized, and this avoids useless warnings
#if defined __cplusplus /*|| __STDC_VERSION__ >= 2023xxL*/
	#define MICROJXL__INIT {}
#else
	#define MICROJXL__INIT {0}
#endif

#ifndef MICROJXL_NODISCARD
	#if __cplusplus >= 201703L /*|| __STDC_VERSION__ >= 2023xxL */
		#define MICROJXL_NODISCARD [[nodiscard]] // since C++17 and C23
	#elif MICROJXL__HAS_WARN_UNUSED_RESULT_ATTR || MICROJXL__GCC_VER >= 0x30400 || MICROJXL__CLANG_VER >= 0x10000
		// this is stronger than [[nodiscard]] in that it's much harder to suppress; we're okay with that
		#define MICROJXL_NODISCARD __attribute__((warn_unused_result)) // since GCC 3.4 and clang 1.0.0
	#else
		#define MICROJXL_NODISCARD
	#endif
#endif // !defined MICROJXL_NODISCARD

#ifndef MICROJXL_MAYBE_UNUSED
	#if __cplusplus >= 201703L /*|| __STDC_VERSION__ >= 2023xxL */
		#define MICROJXL_MAYBE_UNUSED [[maybe_unused]] // since C++17 and C23
	#elif MICROJXL__GCC_VER >= 0x30000 || MICROJXL__CLANG_VER >= 0x10000
		#define MICROJXL_MAYBE_UNUSED __attribute__((unused)) // since GCC 2.95 or earlier (!) and clang 1.0.0
	#else
		#define MICROJXL_MAYBE_UNUSED
	#endif
#endif

// rule of thumb: sparingly use them, except for the obvious error cases
#ifndef MICROJXL_EXPECT
	#if MICROJXL__HAS_BUILTIN_EXPECT || MICROJXL__GCC_VER >= 0x30000
		#define MICROJXL_EXPECT(p, v) __builtin_expect(p, v)
	#else
		#define MICROJXL_EXPECT(p, v) (p)
	#endif
#endif // !defined MICROJXL_EXPECT
#ifndef MICROJXL_LIKELY
	#define MICROJXL_LIKELY(p) MICROJXL_EXPECT(!!(p), 1)
#endif
#ifndef MICROJXL_UNLIKELY
	#define MICROJXL_UNLIKELY(p) MICROJXL_EXPECT(!!(p), 0)
#endif

#if !defined MICROJXL_ADD_OVERFLOW && (MICROJXL__HAS_BUILTIN_ADD_OVERFLOW || MICROJXL__GCC_VER >= 0x50000)
	#define MICROJXL_ADD_OVERFLOW(a, b, res) __builtin_add_overflow(a, b, res)
#endif
#if !defined MICROJXL_SUB_OVERFLOW && (MICROJXL__HAS_BUILTIN_SUB_OVERFLOW || MICROJXL__GCC_VER >= 0x50000)
	#define MICROJXL_SUB_OVERFLOW(a, b, res) __builtin_sub_overflow(a, b, res)
#endif
#if !defined MICROJXL_MUL_OVERFLOW && (MICROJXL__HAS_BUILTIN_MUL_OVERFLOW || MICROJXL__GCC_VER >= 0x50000)
	#define MICROJXL_MUL_OVERFLOW(a, b, res) __builtin_mul_overflow(a, b, res)
#endif

#if !defined MICROJXL_MALLOC && !defined MICROJXL_CALLOC && !defined MICROJXL_REALLOC && !defined MICROJXL_FREE
	#define MICROJXL_MALLOC malloc
	#define MICROJXL_CALLOC calloc
	#define MICROJXL_REALLOC realloc
	#define MICROJXL_FREE free
#elif !(defined MICROJXL_MALLOC && defined MICROJXL_CALLOC && defined MICROJXL_REALLOC && defined MICROJXL_FREE)
	#error "MICROJXL_MALLOC, MICROJXL_CALLOC, MICROJXL_REALLOC and MICROJXL_FREE should be provided altogether."
#endif

// embedded brotli decoder (needed by jbrd JPEG reconstruction and brob boxes)
#include "microjxl_brotli.h"

////////////////////////////////////////////////////////////////////////////////
// state

// bit and logical buffer. this is most frequently accessed and thus available without indirection.
//
// the bit buffer (`nbits` least significant bits of `bits`) is the least significant bits available
// for decoding, and the logical buffer [ptr, end) corresponds to subsequent bits.
// the logical buffer is guaranteed to be all in the codestream (which is not always true if
// the file uses a container).
//
// when the bit buffer has been exhausted the next byte from the logical buffer is consumed and
// appended at the *top* of the bit buffer. when the logical buffer has been exhausted
// higher layers (first backing buffer, then container, and finally source) should be consulted.
typedef struct microjxl__bits_st {
	int32_t nbits; // [0, 64]
	uint64_t bits;
	uint8_t *ptr, *end;
} microjxl__bits_st;

// a common context ("state") for all internal functions.
// this bears a strong similarity with `struct microjxl__inner` type in the API layer which would be
// introduced much later. there are multiple reasons for this split:
// - `microjxl__st` is designed to be in the stack, so it doesn't take up much stack space.
// - `microjxl__st` allows for partial initialization of subsystems, which makes testing much easier.
// - `microjxl__st` only holds things relevant to decoding, while `microjxl__inner` has API contexts.
// - there can be multiple `microjxl__st` for multi-threaded decoding.
typedef struct {
	microjxl_err err; // first error code encountered, or 0
	int saved_errno;
	int cannot_retry; // a fatal error was encountered and no more additional input will fix it

	// different subsystems make use of additional contexts, all accessible from here.
	struct microjxl__bits_st bits; // very frequently accessed, thus inlined here
	struct microjxl__source_st *source;
	struct microjxl__container_st *container;
	struct microjxl__buffer_st *buffer;
	struct microjxl__image_st *image;
	struct microjxl__frame_st *frame;
	struct microjxl__lf_group_st *lf_group;
	const struct microjxl__limits *limits;
} microjxl__st;

////////////////////////////////////////////////////////////////////////////////
// error handling and memory allocation

#ifdef MICROJXL_DEBUG
	#define MICROJXL__ASSERT(cond) assert(cond)
	#define MICROJXL__UNREACHABLE() MICROJXL__ASSERT(0)
#elif MICROJXL__HAS_BUILTIN_UNREACHABLE || MICROJXL__GCC_VER >= 0x40500
	#define MICROJXL__ASSERT(cond) (MICROJXL_UNLIKELY(!(cond)) ? __builtin_unreachable() : (void) 0)
	#define MICROJXL__UNREACHABLE() __builtin_unreachable()
#else
	#define MICROJXL__ASSERT(cond) ((void) (cond))
	#define MICROJXL__UNREACHABLE() ((void) 0) // TODO also check for MSVC __assume
#endif

// MICROJXL_NODISCARD should be before `static` or `inline`
#define MICROJXL__STATIC_RETURNS_ERR MICROJXL_NODISCARD MICROJXL_STATIC microjxl_err
#define MICROJXL__INLINE_RETURNS_ERR MICROJXL_NODISCARD MICROJXL_INLINE microjxl_err

#define MICROJXL__4(s) \
	(microjxl_err) (((uint32_t) (s)[0] << 24) | ((uint32_t) (s)[1] << 16) | ((uint32_t) (s)[2] << 8) | (uint32_t) (s)[3])
#define MICROJXL__ERR(s) microjxl__set_error(st, MICROJXL__4(s))
#define MICROJXL__SHOULD(cond, s) do { \
		if (MICROJXL_UNLIKELY(st->err)) goto MICROJXL__ON_ERROR; \
		if (MICROJXL_UNLIKELY((cond) == 0)) { microjxl__set_error(st, MICROJXL__4(s)); goto MICROJXL__ON_ERROR; } \
	} while (0)
#ifdef MICROJXL_DEBUG
#define MICROJXL__TRACE_RAISE(s) do { if (getenv("MICROJXL_TRACE_ERR")) fprintf(stderr, "[microjxl-raise] \"%s\" at line %d in %s\n", s, __LINE__, __func__); } while (0)
#else
#define MICROJXL__TRACE_RAISE(s) do { } while (0)
#endif
#define MICROJXL__RAISE(s) do { microjxl__set_error(st, MICROJXL__4(s)); MICROJXL__TRACE_RAISE(s); goto MICROJXL__ON_ERROR; } while (0)
#define MICROJXL__RAISE_DELAYED() do { if (MICROJXL_UNLIKELY(st->err)) goto MICROJXL__ON_ERROR; } while (0)
#define MICROJXL__TRY(expr) do { if (MICROJXL_UNLIKELY(expr)) { MICROJXL__ASSERT(st->err); goto MICROJXL__ON_ERROR; } } while (0)

// this *should* use casting because C/C++ don't allow comparison between pointers
// that came from different arrays at all: https://stackoverflow.com/a/39161283
#define MICROJXL__INBOUNDS(ptr, start, size) ((uintptr_t) (ptr) - (uintptr_t) (start) <= (uintptr_t) (size))

#define MICROJXL__TRY_MALLOC(type, ptr, num) \
	do { \
		type *newptr = (type*) microjxl__malloc(num, sizeof(type)); \
		/* check st->err BEFORE assigning: if an error is already pending \
		 * (this is a restarted coroutine pass) the allocation would be \
		 * orphaned by the jump to MICROJXL__ON_ERROR */ \
		if (MICROJXL_UNLIKELY(st->err)) { microjxl__mem_free(newptr); goto MICROJXL__ON_ERROR; } \
		if (MICROJXL_UNLIKELY(!newptr)) { microjxl__set_error(st, MICROJXL__4("!mem")); goto MICROJXL__ON_ERROR; } \
		*(ptr) = newptr; \
	} while (0)

#define MICROJXL__TRY_CALLOC(type, ptr, num) \
	do { \
		type *newptr = (type*) microjxl__calloc(num, sizeof(type)); \
		/* check st->err BEFORE assigning: if an error is already pending \
		 * (this is a restarted coroutine pass) the allocation would be \
		 * orphaned by the jump to MICROJXL__ON_ERROR */ \
		if (MICROJXL_UNLIKELY(st->err)) { microjxl__mem_free(newptr); goto MICROJXL__ON_ERROR; } \
		if (MICROJXL_UNLIKELY(!newptr)) { microjxl__set_error(st, MICROJXL__4("!mem")); goto MICROJXL__ON_ERROR; } \
		*(ptr) = newptr; \
	} while (0)

#define MICROJXL__TRY_REALLOC32(type, ptr, len, cap) \
	do { \
		type *newptr = (type*) microjxl__realloc32(st, *(ptr), sizeof(type), len, cap); \
		if (MICROJXL_LIKELY(newptr)) *(ptr) = newptr; else goto MICROJXL__ON_ERROR; \
	} while (0)

#define MICROJXL__TRY_REALLOC64(type, ptr, len, cap) \
	do { \
		type *newptr = (type*) microjxl__realloc64(st, *(ptr), sizeof(type), len, cap); \
		if (MICROJXL_LIKELY(newptr)) *(ptr) = newptr; else goto MICROJXL__ON_ERROR; \
	} while (0)

MICROJXL_STATIC microjxl_err microjxl__set_error(microjxl__st *st, microjxl_err err);
MICROJXL_STATIC void *microjxl__malloc(size_t num, size_t size);
MICROJXL_STATIC void *microjxl__calloc(size_t num, size_t size);
MICROJXL_STATIC void *microjxl__realloc32(microjxl__st *st, void *ptr, size_t itemsize, int32_t len, int32_t *cap);
MICROJXL_STATIC void *microjxl__realloc64(microjxl__st *st, void *ptr, size_t itemsize, int64_t len, int64_t *cap);
MICROJXL_STATIC void microjxl__mem_free(void *ptr);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_STATIC microjxl_err microjxl__set_error(microjxl__st *st, microjxl_err err) {
	if (err != MICROJXL__4("shrt")) st->cannot_retry = 1;
	if (!st->err) st->err = err;
	return err;
}

MICROJXL_STATIC void *microjxl__malloc(size_t num, size_t size) {
	if (size == 0 || num > SIZE_MAX / size) return NULL;
	return MICROJXL_MALLOC(num * size);
}

MICROJXL_STATIC void *microjxl__calloc(size_t num, size_t size) {
	return MICROJXL_CALLOC(num, size);
}

MICROJXL_STATIC void *microjxl__realloc32(microjxl__st *st, void *ptr, size_t itemsize, int32_t len, int32_t *cap) {
	void *newptr;
	uint32_t newcap;
	size_t newsize;
	MICROJXL__ASSERT(len >= 0);
	if (len <= *cap) return ptr;
	newcap = (uint32_t) *cap * 2;
	if (newcap > (uint32_t) INT32_MAX) newcap = (uint32_t) INT32_MAX;
	if (newcap < (uint32_t) len) newcap = (uint32_t) len;
	MICROJXL__SHOULD(newcap <= SIZE_MAX / itemsize, "!mem");
	newsize = (size_t) (itemsize * newcap);
	MICROJXL__SHOULD(newptr = ptr ? MICROJXL_REALLOC(ptr, newsize) : MICROJXL_MALLOC(newsize), "!mem");
	*cap = (int32_t) newcap;
	return newptr;
MICROJXL__ON_ERROR:
	return NULL;
}

MICROJXL_STATIC void *microjxl__realloc64(microjxl__st *st, void *ptr, size_t itemsize, int64_t len, int64_t *cap) {
	void *newptr;
	uint64_t newcap;
	size_t newsize;
	MICROJXL__ASSERT(len >= 0);
	if (len <= *cap) return ptr;
	newcap = (uint64_t) *cap * 2;
	if (newcap > (uint64_t) INT64_MAX) newcap = (uint64_t) INT64_MAX;
	if (newcap < (uint64_t) len) newcap = (uint64_t) len;
	MICROJXL__SHOULD(newcap <= SIZE_MAX / itemsize, "!mem");
	newsize = (size_t) (itemsize * newcap);
	MICROJXL__SHOULD(newptr = ptr ? MICROJXL_REALLOC(ptr, newsize) : MICROJXL_MALLOC(newsize), "!mem");
	*cap = (int64_t) newcap;
	return newptr;
MICROJXL__ON_ERROR:
	return NULL;
}

MICROJXL_STATIC void microjxl__mem_free(void *ptr) {
	MICROJXL_FREE(ptr);
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// utility

#define MICROJXL__CONCAT_(a,b) a##b
#define MICROJXL__CONCAT(a,b) MICROJXL__CONCAT_(a,b)
#define MICROJXL__CONCAT3(a,b,c) MICROJXL__CONCAT(a,MICROJXL__CONCAT(b,c))

// `microjxl__(foo, X)` and its uppercase version is `microjxl__foo` followed by a macro `MICROJXL__V` expanded;
// this greatly simplifies the construction of templated names.
#define MICROJXL__PARAMETRIC_NAME_(prefix, x, MICROJXL__V) MICROJXL__CONCAT3(prefix, x, MICROJXL__V)
#define microjxl__(x, V) MICROJXL__PARAMETRIC_NAME_(microjxl__, x, MICROJXL__CONCAT(MICROJXL__, V))
#define MICROJXL__(x, V) MICROJXL__PARAMETRIC_NAME_(MICROJXL__, x, MICROJXL__CONCAT(MICROJXL__, V))

MICROJXL_ALWAYS_INLINE int32_t microjxl__unpack_signed(int32_t x);
MICROJXL_ALWAYS_INLINE int64_t microjxl__unpack_signed64(int64_t x);
MICROJXL_ALWAYS_INLINE int32_t microjxl__ceil_div32(int32_t x, int32_t y);
MICROJXL_ALWAYS_INLINE int64_t microjxl__ceil_div64(int64_t x, int64_t y);
MICROJXL_ALWAYS_INLINE float microjxl__minf(float x, float y);
MICROJXL_ALWAYS_INLINE float microjxl__maxf(float x, float y);
MICROJXL_ALWAYS_INLINE int microjxl__surely_nonzero(float x);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_ALWAYS_INLINE int32_t microjxl__unpack_signed(int32_t x) {
	/* libjxl UnpackSigned operates on the *unsigned* code: zigzag
	 * ((value >> 1) ^ -(value & 1)). Interpreting the code as signed first
	 * (e.g. via division) breaks for codes >= 2^31, which 32-bit float
	 * modular samples legitimately produce. */
	return (int32_t) (((uint32_t) x >> 1) ^ (uint32_t) (-(int32_t) (x & 1)));
}
MICROJXL_ALWAYS_INLINE int64_t microjxl__unpack_signed64(int64_t x) {
	return (int64_t) (x & 1 ? -(x / 2 + 1) : x / 2);
}

// equivalent to ceil(x / y)
MICROJXL_ALWAYS_INLINE int32_t microjxl__ceil_div32(int32_t x, int32_t y) { return (x + y - 1) / y; }
MICROJXL_ALWAYS_INLINE int64_t microjxl__ceil_div64(int64_t x, int64_t y) { return (x + y - 1) / y; }

MICROJXL_ALWAYS_INLINE float microjxl__minf(float x, float y) { return (x < y ? x : y); }
MICROJXL_ALWAYS_INLINE float microjxl__maxf(float x, float y) { return (x > y ? x : y); }

/* fused multiply-add, matching libjxl's highway MulAdd bit-for-bit when the
 * target has hardware FMA (FP_FAST_FMAF). Without it the unfused fallback
 * differs only in intermediate rounding (sub-ULP visual difference). */
MICROJXL_ALWAYS_INLINE float microjxl__fmaf(float a, float b, float c) {
#ifdef FP_FAST_FMAF
	return fmaf(a, b, c);
#else
	return a * b + c;
#endif
}

// used to guard against division by zero
MICROJXL_ALWAYS_INLINE int microjxl__surely_nonzero(float x) {
	return isfinite(x) && fabs(x) >= 1e-8f;
}

#ifdef _MSC_VER // required for microjxl__floor/ceil_lgN implementations

#include <intrin.h>

#pragma intrinsic(_BitScanReverse)
MICROJXL_ALWAYS_INLINE int microjxl__clz32(uint32_t x) {
	unsigned long index;
	return _BitScanReverse(&index, x) ? 31 - (int) index : 32;
}

MICROJXL_ALWAYS_INLINE int microjxl__clz16(uint16_t x) { return microjxl__clz32(x); }

// _BitScanReverse64 is not available at all in x86-32, so we need to detour
#if defined __ia64__ || defined __x86_64
#pragma intrinsic(_BitScanReverse64)
MICROJXL_ALWAYS_INLINE int microjxl__clz64(uint64_t x) {
	unsigned long index;
	return _BitScanReverse64(&index, x) ? 63 - (int) index : 64;
}
#else
MICROJXL_ALWAYS_INLINE int microjxl__clz64(uint64_t x) {
	return x >> 32 ? microjxl__clz32((uint32_t) (x >> 32)) : 32 + microjxl__clz32((uint32_t) x);
}
#endif // defined __ia64__ || defined __x86_64

#endif // defined _MSC_VER

#endif // defined MICROJXL_IMPLEMENTATION

// ----------------------------------------
// recursion for bit-dependent math functions
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING 100
#define MICROJXL__N 16
#include MICROJXL_FILENAME
#define MICROJXL__N 32
#include MICROJXL_FILENAME
#define MICROJXL__N 64
#include MICROJXL_FILENAME
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING (-1)

#endif // MICROJXL__RECURSING < 0
#if MICROJXL__RECURSING == 100
	#define microjxl__intN MICROJXL__CONCAT3(int, MICROJXL__N, _t)
	#define microjxl__uintN MICROJXL__CONCAT3(uint, MICROJXL__N, _t)
	#define MICROJXL__INTN_MAX MICROJXL__CONCAT3(INT, MICROJXL__N, _MAX)
	#define MICROJXL__INTN_MIN MICROJXL__CONCAT3(INT, MICROJXL__N, _MIN)
// ----------------------------------------

MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(floor_avg,N)(microjxl__intN x, microjxl__intN y);
MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(abs,N)(microjxl__intN x);
MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(min,N)(microjxl__intN x, microjxl__intN y);
MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(max,N)(microjxl__intN x, microjxl__intN y);

// returns 1 if overflow or underflow didn't occur
MICROJXL_ALWAYS_INLINE int microjxl__(add,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out);
MICROJXL_ALWAYS_INLINE int microjxl__(sub,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out);
MICROJXL_ALWAYS_INLINE int microjxl__(mul,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out);
MICROJXL_ALWAYS_INLINE int microjxl__(add_fallback,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out);
MICROJXL_ALWAYS_INLINE int microjxl__(sub_fallback,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out);
MICROJXL_ALWAYS_INLINE int microjxl__(mul_fallback,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out);
MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(clamp_add,N)(microjxl__intN x, microjxl__intN y);
MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(clamp_mul,N)(microjxl__intN x, microjxl__intN y);

#ifdef MICROJXL_IMPLEMENTATION

// same to `(a + b) >> 1` but doesn't overflow, useful for tight loops with autovectorization
// https://devblogs.microsoft.com/oldnewthing/20220207-00/?p=106223
MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(floor_avg,N)(microjxl__intN x, microjxl__intN y) {
	return (microjxl__intN) (x / 2 + y / 2 + (x & y & 1));
}

MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(abs,N)(microjxl__intN x) {
	return (microjxl__intN) (x < 0 ? -x : x);
}
MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(min,N)(microjxl__intN x, microjxl__intN y) {
	return (microjxl__intN) (x < y ? x : y);
}
MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(max,N)(microjxl__intN x, microjxl__intN y) {
	return (microjxl__intN) (x > y ? x : y);
}

MICROJXL_ALWAYS_INLINE int microjxl__(add,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out) {
#ifdef MICROJXL_ADD_OVERFLOW
	// gcc/clang extension uses an opposite convention, which is unnatural to use with MICROJXL__SHOULD
	return !MICROJXL_ADD_OVERFLOW(x, y, out);
#else
	return microjxl__(add_fallback,N)(x, y, out);
#endif
}

MICROJXL_ALWAYS_INLINE int microjxl__(sub,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out) {
#ifdef MICROJXL_SUB_OVERFLOW
	return !MICROJXL_SUB_OVERFLOW(x, y, out);
#else
	return microjxl__(sub_fallback,N)(x, y, out);
#endif
}

MICROJXL_ALWAYS_INLINE int microjxl__(mul,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out) {
#ifdef MICROJXL_MUL_OVERFLOW
	return !MICROJXL_MUL_OVERFLOW(x, y, out);
#else
	return microjxl__(mul_fallback,N)(x, y, out);
#endif
}

MICROJXL_ALWAYS_INLINE int microjxl__(add_fallback,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out) {
	if (MICROJXL_UNLIKELY((x > 0 && y > MICROJXL__INTN_MAX - x) || (x < 0 && y < MICROJXL__INTN_MIN - x))) {
		return 0;
	} else {
		*out = (microjxl__intN) (x + y);
		return 1;
	}
}

MICROJXL_ALWAYS_INLINE int microjxl__(sub_fallback,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out) {
	if (MICROJXL_UNLIKELY((y < 0 && x > MICROJXL__INTN_MAX + y) || (y > 0 && x < MICROJXL__INTN_MIN + y))) {
		return 0;
	} else {
		*out = (microjxl__intN) (x - y);
		return 1;
	}
}

MICROJXL_ALWAYS_INLINE int microjxl__(mul_fallback,N)(microjxl__intN x, microjxl__intN y, microjxl__intN *out) {
	if (MICROJXL_UNLIKELY(
		x > 0 ?
			(y > 0 ? x > MICROJXL__INTN_MAX / y : y < MICROJXL__INTN_MIN / x) :
			(y > 0 ? x < MICROJXL__INTN_MIN / y : y != 0 && x < MICROJXL__INTN_MAX / y)
	)) {
		return 0;
	} else {
		*out = (microjxl__intN) (x * y);
		return 1;
	}
}

MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(clamp_add,N)(microjxl__intN x, microjxl__intN y) {
	microjxl__intN out;
	return microjxl__(add,N)(x, y, &out) ? out : MICROJXL__INTN_MAX;
}

MICROJXL_ALWAYS_INLINE microjxl__intN microjxl__(clamp_mul,N)(microjxl__intN x, microjxl__intN y) {
	microjxl__intN out;
	return microjxl__(mul,N)(x, y, &out) ? out : MICROJXL__INTN_MAX;
}

#endif // defined MICROJXL_IMPLEMENTATION

#ifdef _MSC_VER
	#define MICROJXL__CLZN microjxl__(clz, N)
#else
	#define MICROJXL__UINTN_MAX MICROJXL__CONCAT3(UINT, MICROJXL__N, _MAX)
	#if UINT_MAX == MICROJXL__UINTN_MAX
			#define MICROJXL__CLZN __builtin_clz
	#elif ULONG_MAX == MICROJXL__UINTN_MAX
			#define MICROJXL__CLZN __builtin_clzl
	#elif ULLONG_MAX == MICROJXL__UINTN_MAX
			#define MICROJXL__CLZN __builtin_clzll
	#endif
	#undef MICROJXL__UINTN_MAX
#endif // !defined _MSC_VER
#ifdef MICROJXL__CLZN
	MICROJXL_ALWAYS_INLINE int microjxl__(floor_lg,N)(microjxl__uintN x);
	MICROJXL_ALWAYS_INLINE int microjxl__(ceil_lg,N)(microjxl__uintN x);

	#ifdef MICROJXL_IMPLEMENTATION
	// both requires x to be > 0
	MICROJXL_ALWAYS_INLINE int microjxl__(floor_lg,N)(microjxl__uintN x) {
		return MICROJXL__N - 1 - MICROJXL__CLZN(x);
	}
	MICROJXL_ALWAYS_INLINE int microjxl__(ceil_lg,N)(microjxl__uintN x) {
		return x > 1 ? MICROJXL__N - MICROJXL__CLZN(x - 1) : 0;
	}
	#endif

	#undef MICROJXL__CLZN
#endif

// ----------------------------------------
// end of recursion
	#undef microjxl__intN
	#undef microjxl__uintN
	#undef MICROJXL__INTN_MAX
	#undef MICROJXL__INTN_MIN
	#undef MICROJXL__N
#endif // MICROJXL__RECURSING == 100
#if MICROJXL__RECURSING < 0
// ----------------------------------------

////////////////////////////////////////////////////////////////////////////////
// aligned pointers

#ifndef MICROJXL_ASSUME_ALIGNED
	#if MICROJXL__HAS_BUILTIN_ASSUME_ALIGNED || MICROJXL__GCC_VER >= 0x40700
		#define MICROJXL_ASSUME_ALIGNED(p, align) __builtin_assume_aligned(p, align)
	#else
		#define MICROJXL_ASSUME_ALIGNED(p, align) (p)
	#endif
#endif // !defined MICROJXL_ASSUME_ALIGNED

MICROJXL_ALWAYS_INLINE void *microjxl__alloc_aligned(size_t sz, size_t align, size_t *outmisalign);
MICROJXL_ALWAYS_INLINE void microjxl__mem_free_aligned(void *ptr, size_t align, size_t misalign);

MICROJXL_MAYBE_UNUSED MICROJXL_STATIC void *microjxl__alloc_aligned_fallback(size_t sz, size_t align, size_t *outmisalign);
MICROJXL_MAYBE_UNUSED MICROJXL_STATIC void microjxl__mem_free_aligned_fallback(void *ptr, size_t align, size_t misalign);

#ifdef MICROJXL_IMPLEMENTATION

#if _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600
	MICROJXL_ALWAYS_INLINE void *microjxl__alloc_aligned(size_t sz, size_t align, size_t *outmisalign) {
		void *ptr = NULL;
		*outmisalign = 0;
		return posix_memalign(&ptr, align, sz) ? NULL : ptr;
	}
	MICROJXL_ALWAYS_INLINE void microjxl__mem_free_aligned(void *ptr, size_t align, size_t misalign) {
		(void) align; (void) misalign;
		free(ptr); // important: do not use microjxl_free!
	}
#elif defined _ISOC11_SOURCE
	MICROJXL_ALWAYS_INLINE void *microjxl__alloc_aligned(size_t sz, size_t align, size_t *outmisalign) {
		if (sz > SIZE_MAX / align * align) return NULL; // overflow
		*outmisalign = 0;
		return aligned_alloc(align, (sz + align - 1) / align * align);
	}
	MICROJXL_ALWAYS_INLINE void microjxl__mem_free_aligned(void *ptr, size_t align, size_t misalign) {
		(void) align; (void) misalign;
		free(ptr); // important: do not use microjxl_free!
	}
#else
	MICROJXL_ALWAYS_INLINE void *microjxl__alloc_aligned(size_t sz, size_t align, size_t *outmisalign) {
		return microjxl__alloc_aligned_fallback(sz, align, outmisalign);
	}
	MICROJXL_ALWAYS_INLINE void microjxl__mem_free_aligned(void *ptr, size_t align, size_t misalign) {
		microjxl__mem_free_aligned_fallback(ptr, align, misalign);
	}
#endif

// a fallback implementation; the caller should store the misalign amount [0, align) separately.
// used when the platform doesn't provide aligned malloc at all, or the platform implementation
// is not necessarily better; e.g. MSVC _aligned_malloc has the same amount of overhead as of Win10
MICROJXL_MAYBE_UNUSED MICROJXL_STATIC void *microjxl__alloc_aligned_fallback(size_t sz, size_t align, size_t *outmisalign) {
	// while this is almost surely an overestimate (can be improved if we know the malloc alignment)
	// there is no standard way to compute a better estimate in C99 so this is inevitable.
	size_t maxmisalign = align - 1, misalign;
	void *ptr;
	if (sz > SIZE_MAX - maxmisalign) return NULL; // overflow
	ptr = MICROJXL_MALLOC(sz + maxmisalign);
	if (!ptr) return NULL;
	misalign = align - (uintptr_t) ptr % align;
	if (misalign == align) misalign = 0;
	*outmisalign = misalign;
	return (void*) ((uintptr_t) ptr + misalign);
}

MICROJXL_MAYBE_UNUSED MICROJXL_ALWAYS_INLINE void microjxl__mem_free_aligned_fallback(void *ptr, size_t align, size_t misalign) {
	if (!ptr) return;
	MICROJXL__ASSERT((uintptr_t) ptr % align == 0);
	microjxl__mem_free((void*) ((uintptr_t) ptr - misalign));
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// two-dimensional view

typedef struct { int32_t logw, logh; float *MICROJXL_RESTRICT ptr; } microjxl__view_f32;

MICROJXL_ALWAYS_INLINE microjxl__view_f32 microjxl__make_view_f32(int32_t logw, int32_t logh, float *MICROJXL_RESTRICT ptr);
MICROJXL_ALWAYS_INLINE void microjxl__adapt_view_f32(microjxl__view_f32 *outv, int32_t logw, int32_t logh);
MICROJXL_ALWAYS_INLINE void microjxl__reshape_view_f32(microjxl__view_f32 *outv, int32_t logw, int32_t logh);
MICROJXL_ALWAYS_INLINE void microjxl__copy_view_f32(microjxl__view_f32 *outv, const microjxl__view_f32 inv);
MICROJXL_ALWAYS_INLINE void microjxl__transpose_view_f32(microjxl__view_f32 *outv, const microjxl__view_f32 inv);
MICROJXL_ALWAYS_INLINE void microjxl__oddeven_columns_to_halves_f32(microjxl__view_f32 *outv, const microjxl__view_f32 inv);
MICROJXL_ALWAYS_INLINE void microjxl__oddeven_rows_to_halves_f32(microjxl__view_f32 *outv, const microjxl__view_f32 inv);
MICROJXL_MAYBE_UNUSED MICROJXL_STATIC void microjxl__print_view_f32(microjxl__view_f32 v, const char *name, const char *file, int32_t line);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_ALWAYS_INLINE microjxl__view_f32 microjxl__make_view_f32(int32_t logw, int32_t logh, float *MICROJXL_RESTRICT ptr) {
	microjxl__view_f32 ret = { logw, logh, ptr };
	return ret;
}

MICROJXL_ALWAYS_INLINE void microjxl__adapt_view_f32(microjxl__view_f32 *outv, int32_t logw, int32_t logh) {
	MICROJXL__ASSERT(outv->logw + outv->logh >= logw + logh);
	outv->logw = logw;
	outv->logh = logh;
}

MICROJXL_ALWAYS_INLINE void microjxl__reshape_view_f32(microjxl__view_f32 *outv, int32_t logw, int32_t logh) {
	MICROJXL__ASSERT(outv->logw + outv->logh == logw + logh);
	outv->logw = logw;
	outv->logh = logh;
}

MICROJXL_ALWAYS_INLINE void microjxl__copy_view_f32(microjxl__view_f32 *outv, const microjxl__view_f32 inv) {
	int32_t x, y;
	float *outptr = outv->ptr;
	microjxl__adapt_view_f32(outv, inv.logw, inv.logh);
	for (y = 0; y < (1 << inv.logh); ++y) for (x = 0; x < (1 << inv.logw); ++x) {
		outptr[y << inv.logw | x] = inv.ptr[y << inv.logw | x];
	}
}

MICROJXL_ALWAYS_INLINE void microjxl__transpose_view_f32(microjxl__view_f32 *outv, const microjxl__view_f32 inv) {
	int32_t x, y;
	float *outptr = outv->ptr;
	microjxl__adapt_view_f32(outv, inv.logh, inv.logw);
	for (y = 0; y < (1 << inv.logh); ++y) for (x = 0; x < (1 << inv.logw); ++x) {
		outptr[x << inv.logh | y] = inv.ptr[y << inv.logw | x];
	}
}

// shuffles columns 01234567 into 02461357 and so on
MICROJXL_ALWAYS_INLINE void microjxl__oddeven_columns_to_halves_f32(microjxl__view_f32 *outv, const microjxl__view_f32 inv) {
	int32_t x, y;
	float *outptr = outv->ptr;
	MICROJXL__ASSERT(inv.logw > 0);
	microjxl__adapt_view_f32(outv, inv.logw, inv.logh);
	for (y = 0; y < (1 << inv.logh); ++y) for (x = 0; x < (1 << inv.logw); ++x) {
		int32_t outx = ((x & 1) << (inv.logw - 1)) | (x >> 1);
		outptr[y << inv.logw | outx] = inv.ptr[y << inv.logw | x];
	}
}

// shuffles rows 01234567 into 02461357 and so on
MICROJXL_ALWAYS_INLINE void microjxl__oddeven_rows_to_halves_f32(microjxl__view_f32 *outv, const microjxl__view_f32 inv) {
	int32_t x, y;
	float *outptr = outv->ptr;
	MICROJXL__ASSERT(inv.logh > 0);
	microjxl__adapt_view_f32(outv, inv.logw, inv.logh);
	for (y = 0; y < (1 << inv.logh); ++y) {
		int32_t outy = ((y & 1) << (inv.logh - 1)) | (y >> 1);
		for (x = 0; x < (1 << inv.logw); ++x) outptr[outy << inv.logw | x] = inv.ptr[y << inv.logw | x];
	}
}

#define MICROJXL__AT(view, x, y) \
	(MICROJXL__ASSERT(0 <= (x) && (x) < (1 << (view).logw) && 0 <= (y) && (y) < (1 << (view).logh)), \
	 (view).ptr + ((y) << (view).logw | (x)))

#define MICROJXL__VIEW_FOREACH(view, y, x, v) \
	for (y = 0; y < (1 << (view).logh); ++y) \
		for (x = 0; x < (1 << (view).logw) && (v = (view).ptr + (y << (view).logw | x), 1); ++x)

MICROJXL_MAYBE_UNUSED MICROJXL_STATIC void microjxl__print_view_f32(microjxl__view_f32 v, const char *name, const char *file, int32_t line) {
	int32_t x, y;        printf(".--- %s:%d: %s (w=%d h=%d @%p)", file, line, name, 1 << v.logw, 1 << v.logh, (void *) v.ptr);
	for (y = 0; y < (1 << v.logh); ++y) {
		printf("\n|");
		for (x = 0; x < (1 << v.logw); ++x) printf(" %f", *MICROJXL__AT(v, x, y));
	}
	printf("\n'--- %s:%d\n", file, line);
}

#endif // defined MICROJXL_IMPLEMENTATION

#define microjxl__print_view_f32(v) microjxl__print_view_f32(v, #v, __FILE__, __LINE__)

////////////////////////////////////////////////////////////////////////////////
// plane

enum {
	MICROJXL__PLANE_U8 = (uint8_t) 0x20,
	MICROJXL__PLANE_U16 = (uint8_t) 0x21,
	MICROJXL__PLANE_I16 = (uint8_t) 0x41,
	MICROJXL__PLANE_U32 = (uint8_t) 0x22,
	MICROJXL__PLANE_I32 = (uint8_t) 0x42,
	MICROJXL__PLANE_F32 = (uint8_t) 0x62,
	/* half-float plane (2 bytes/sample): carrier for the VarDCT render
	 * pipeline's display-referred floats (libjxl keeps pipeline rows as
	 * float16; converting to full float would quantize differently). */
	MICROJXL__PLANE_F16 = (uint8_t) 0x61,
	MICROJXL__PLANE_EMPTY = (uint8_t) 0xe0, // should have width=0 and height=0
};

#define MICROJXL__PIXELS_ALIGN 32

typedef struct {
	uint8_t type; // 0 means uninitialized (all fields besides from pixels are considered garbage)
	uint8_t misalign;
	int8_t vshift, hshift;
	int32_t width, height;
	int32_t stride_bytes; // the number of *bytes* between each row
	uintptr_t pixels;
} microjxl__plane;

#define MICROJXL__TYPED_PIXELS(plane, y, typeconst, pixel_t) \
	(MICROJXL__ASSERT((plane)->type == typeconst), \
	 MICROJXL__ASSERT(0 <= (y) && (y) < (plane)->height), \
	 (pixel_t*) MICROJXL_ASSUME_ALIGNED( \
		(void*) ((char*) (plane)->pixels + (size_t) (plane)->stride_bytes * (size_t) (y)), \
		MICROJXL__PIXELS_ALIGN))

#define MICROJXL__U8_PIXELS(plane, y) MICROJXL__TYPED_PIXELS(plane, y, MICROJXL__PLANE_U8, uint8_t)
#define MICROJXL__U16_PIXELS(plane, y) MICROJXL__TYPED_PIXELS(plane, y, MICROJXL__PLANE_U16, uint16_t)
#define MICROJXL__I16_PIXELS(plane, y) MICROJXL__TYPED_PIXELS(plane, y, MICROJXL__PLANE_I16, int16_t)
#define MICROJXL__U32_PIXELS(plane, y) MICROJXL__TYPED_PIXELS(plane, y, MICROJXL__PLANE_U32, uint32_t)
#define MICROJXL__I32_PIXELS(plane, y) MICROJXL__TYPED_PIXELS(plane, y, MICROJXL__PLANE_I32, int32_t)
#define MICROJXL__F32_PIXELS(plane, y) MICROJXL__TYPED_PIXELS(plane, y, MICROJXL__PLANE_F32, float)
#define MICROJXL__F16_PIXELS(plane, y) MICROJXL__TYPED_PIXELS(plane, y, MICROJXL__PLANE_F16, uint16_t)

#define MICROJXL__PLANE_PIXEL_SIZE(plane) (1 << ((plane)->type & 31))
#define MICROJXL__PLANE_STRIDE(plane) ((plane)->stride_bytes >> ((plane)->type & 31))

enum {
	MICROJXL__PLANE_CLEAR = 1 << 0,
	// for public facing planes, we always add padding to prevent misconception
	MICROJXL__PLANE_FORCE_PAD = 1 << 1,
};

MICROJXL__STATIC_RETURNS_ERR microjxl__init_plane(
	microjxl__st *st, uint8_t type, int32_t width, int32_t height, int flags, microjxl__plane *out
);
MICROJXL_STATIC void microjxl__init_empty_plane(microjxl__plane *out);
MICROJXL_STATIC int microjxl__plane_all_equal_sized(const microjxl__plane *begin, const microjxl__plane *end);
// returns that type if all planes have the same type, otherwise returns 0
MICROJXL_STATIC uint8_t microjxl__plane_all_equal_typed(const microjxl__plane *begin, const microjxl__plane *end);
MICROJXL_STATIC uint8_t microjxl__plane_all_equal_typed_or_empty(const microjxl__plane *begin, const microjxl__plane *end);
MICROJXL_STATIC void microjxl__mem_free_plane(microjxl__plane *plane);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__init_plane(
	microjxl__st *st, uint8_t type, int32_t width, int32_t height, int flags, microjxl__plane *out
) {
	int32_t pixel_size = 1 << (type & 31);
	void *pixels;
	int32_t stride_bytes;
	size_t total, misalign;

	out->type = 0;
	MICROJXL__ASSERT(width > 0 && height > 0);

	MICROJXL__SHOULD(microjxl__mul32(width, pixel_size, &stride_bytes), "bigg");
	if (flags & MICROJXL__PLANE_FORCE_PAD) MICROJXL__SHOULD(microjxl__add32(stride_bytes, 1, &stride_bytes), "bigg");
	MICROJXL__SHOULD(
		microjxl__mul32(microjxl__ceil_div32(stride_bytes, MICROJXL__PIXELS_ALIGN), MICROJXL__PIXELS_ALIGN, &stride_bytes),
		"bigg");
	MICROJXL__SHOULD((size_t) stride_bytes <= SIZE_MAX / (uint32_t) height, "bigg");
	total = (size_t) stride_bytes * (size_t) height;
	MICROJXL__SHOULD(pixels = microjxl__alloc_aligned(total, MICROJXL__PIXELS_ALIGN, &misalign), "!mem");

	out->stride_bytes = stride_bytes;
	out->width = width;
	out->height = height;
	out->type = type;
	out->vshift = out->hshift = 0;
	out->misalign = (uint8_t) misalign;
	out->pixels = (uintptr_t) pixels;
	if (flags & MICROJXL__PLANE_CLEAR) memset(pixels, 0, total);

MICROJXL__ON_ERROR:
	return st->err;
}

// an empty plane can arise from inverse modular transform, but it can be a bug as well,
// hence a separate function and separate type.
MICROJXL_STATIC void microjxl__init_empty_plane(microjxl__plane *out) {
	out->type = MICROJXL__PLANE_EMPTY;
	out->stride_bytes = 0;
	out->width = out->height = 0;
	out->vshift = out->hshift = 0;
	out->misalign = 0;
	out->pixels = (uintptr_t) (void*) 0;
}

MICROJXL_STATIC int microjxl__plane_all_equal_sized(const microjxl__plane *begin, const microjxl__plane *end) {
	microjxl__plane c;
	int shift_should_match;
	if (begin >= end) return 0; // do not allow edge cases
	c = *begin;
	shift_should_match = (begin->vshift >= 0 && begin->hshift >= 0);
	while (++begin < end) {
		if (c.width != begin->width || c.height != begin->height) return 0;
		// even though the sizes match, different shifts can't be mixed as per the spec
		if (shift_should_match) {
			if (c.vshift >= 0 && c.hshift >= 0 && (c.vshift != begin->vshift || c.hshift != begin->hshift)) return 0;
		}
	}
	return 1;
}

MICROJXL_STATIC uint8_t microjxl__plane_all_equal_typed(const microjxl__plane *begin, const microjxl__plane *end) {
	uint8_t type;
	if (begin >= end) return 0;
	type = begin->type;
	while (++begin < end) {
		if (begin->type != type) return 0;
	}
	return type;
}

MICROJXL_STATIC uint8_t microjxl__plane_all_equal_typed_or_empty(const microjxl__plane *begin, const microjxl__plane *end) {
	uint8_t type;
	if (begin >= end) return 0;
	type = begin->type;
	while (++begin < end) {
		// allow empty plane to pass this test; if all planes are empty, will return MICROJXL__PLANE_EMPTY
		if (type == MICROJXL__PLANE_EMPTY) type = begin->type;
		if (begin->type != MICROJXL__PLANE_EMPTY && begin->type != type) return 0;
	}
	return type;
}

MICROJXL_STATIC void microjxl__mem_free_plane(microjxl__plane *plane) {
	// we don't touch pixels if plane is zero-initialized via memset, because while `plane->type` is
	// definitely zero in this case `(void*) plane->pixels` might NOT be a null pointer!
	if (plane->type && plane->type != MICROJXL__PLANE_EMPTY) {
		microjxl__mem_free_aligned((void*) plane->pixels, MICROJXL__PIXELS_ALIGN, plane->misalign);
	}
	plane->width = plane->height = plane->stride_bytes = 0;
	plane->type = 0;
	plane->vshift = plane->hshift = 0;
	plane->misalign = 0;
	plane->pixels = (uintptr_t) (void*) 0; 
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// noise (K.5)

MICROJXL__STATIC_RETURNS_ERR microjxl__add_noise_frame(microjxl__st *st);
MICROJXL__STATIC_RETURNS_ERR microjxl__finalize_vardct_frame(microjxl__st *st);

////////////////////////////////////////////////////////////////////////////////
// limits

typedef struct microjxl__limits {
	int64_t pixels; // <= 2^61
	int32_t width; // <= 2^31
	int32_t height; // <= 2^30

	uint64_t icc_output_size; // < 2^63
	int32_t bpp; // <= 64
	int ec_black_allowed;

	int32_t num_extra_channels; // <= 4096
	int needs_modular_16bit_buffers;
	int32_t nb_transforms; // <= 273

	int32_t nb_channels_tr; // # of modular channels after transform
	int32_t tree_depth; // distance between root and leaf nodes
	int64_t zf_pixels; // total # of pixels in a run of zero-duration frames, unlimited if zero
} microjxl__limits;

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_STATIC const microjxl__limits MICROJXL__MAIN_LV5_LIMITS = {
	/*.pixels =*/ 1 << 28, /*.width =*/ 1 << 18, /*.height =*/ 1 << 18,
	/*.icc_output_size =*/ 1u << 22, /*.bpp =*/ 32, /*.ec_black_allowed =*/ 0,
	/*.num_extra_channels =*/ 4, /*.needs_modular_16bit_buffers =*/ 0, /*.nb_transforms =*/ 8,
	/*.nb_channels_tr =*/ 256, /*.tree_depth =*/ 64, /*.zf_pixels =*/ 1 << 28,
};

/* Table A.3: level 10 limits. Larger images, more extra channels and
 * transforms, deeper trees, and black extra channels are allowed. */
MICROJXL_STATIC const microjxl__limits MICROJXL__MAIN_LV10_LIMITS = {
	/*.pixels =*/ (int64_t) 1 << 40, /*.width =*/ 1 << 30, /*.height =*/ 1 << 30,
	/*.icc_output_size =*/ 1u << 28, /*.bpp =*/ 32, /*.ec_black_allowed =*/ 1,
	/*.num_extra_channels =*/ 256, /*.needs_modular_16bit_buffers =*/ 0, /*.nb_transforms =*/ 512,
	/*.nb_channels_tr =*/ 1 << 16, /*.tree_depth =*/ 2048, /*.zf_pixels =*/ 0,
};

#endif // defined MICROJXL_IMPLEMENTATION

extern const microjxl__limits MICROJXL__MAIN_LV5_LIMITS/*, MICROJXL__MAIN_LV10_LIMITS*/;

////////////////////////////////////////////////////////////////////////////////
// input source

typedef int (*microjxl_source_read_func)(uint8_t *buf, int64_t fileoff, size_t maxsize, size_t *size, void *data);
typedef int (*microjxl_source_seek_func)(int64_t fileoff, void *data);
typedef void (*microjxl_source_free_func)(void *data); // intentionally same to microjxl_memory_free_func

typedef struct microjxl__source_st {
	microjxl_source_read_func read_func;
	microjxl_source_seek_func seek_func;
	microjxl_source_free_func free_func;
	void *data;

	int64_t fileoff; // absolute file offset, assumed to be 0 at the initialization
	int64_t fileoff_limit; // fileoff can't exceed this; otherwise will behave as if EOF has occurred
} microjxl__source_st;

MICROJXL__STATIC_RETURNS_ERR microjxl__init_memory_source(
	microjxl__st *st, uint8_t *buf, size_t size, microjxl_memory_free_func freefunc, microjxl__source_st *source
);
MICROJXL__STATIC_RETURNS_ERR microjxl__init_file_source(microjxl__st *st, const char *path, microjxl__source_st *source);
MICROJXL__STATIC_RETURNS_ERR microjxl__try_read_from_source(
	microjxl__st *st, uint8_t *buf, int64_t minsize, int64_t maxsize, int64_t *size
);
MICROJXL__STATIC_RETURNS_ERR microjxl__read_from_source(microjxl__st *st, uint8_t *buf, int64_t size);
MICROJXL__STATIC_RETURNS_ERR microjxl__seek_from_source(microjxl__st *st, int64_t fileoff);
MICROJXL_STATIC void microjxl__mem_free_source(microjxl__source_st *source);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_STATIC int microjxl__memory_source_read(uint8_t *buf, int64_t fileoff, size_t maxsize, size_t *size, void *data) {
	uint8_t *mem = (uint8_t*) data;
	memcpy(buf, mem + fileoff, maxsize);
	*size = maxsize;
	return 0;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__init_memory_source(
	microjxl__st *st, uint8_t *buf, size_t size, microjxl_memory_free_func freefunc, microjxl__source_st *source
) {
	MICROJXL__SHOULD(size <= (uint64_t) INT64_MAX, "flen");
	source->read_func = microjxl__memory_source_read;
	source->seek_func = NULL;
	source->free_func = freefunc;
	source->data = buf;
	source->fileoff = 0;
	source->fileoff_limit = (int64_t) size;
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC int microjxl__file_source_read(uint8_t *buf, int64_t fileoff, size_t maxsize, size_t *size, void *data) {
	FILE *fp = (FILE*) data;
	size_t read;

	(void) fileoff;
	read = fread(buf, 1, maxsize, fp);
	if (read > 0) {
		*size = read;
		return 0;
	} else if (feof(fp)) {
		*size = 0;
		return 0;
	} else {
		return 1;
	}
}

MICROJXL_STATIC int microjxl__file_source_seek(int64_t fileoff, void *data) {
	FILE *fp = (FILE*) data;
	if (fileoff < 0) return 1;
	if (fileoff <= LONG_MAX) {
		if (fseek(fp, (long) fileoff, SEEK_SET) != 0) return 1;
	} else {
		if (fseek(fp, LONG_MAX, SEEK_SET) != 0) return 1;
		fileoff -= LONG_MAX;
		while (fileoff >= LONG_MAX) {
			if (fseek(fp, LONG_MAX, SEEK_CUR) != 0) return 1;
			fileoff -= LONG_MAX;
		}
		if (fseek(fp, (long) fileoff, SEEK_CUR) != 0) return 1;
	}
	return 0;
}

MICROJXL_STATIC void microjxl__file_source_free(void *data) {
	FILE *fp = (FILE*) data;
	fclose(fp);
}

MICROJXL__STATIC_RETURNS_ERR microjxl__init_file_source(microjxl__st *st, const char *path, microjxl__source_st *source) {
	FILE *fp;
	int saved_errno;
	
	saved_errno = errno;
	errno = 0;
	fp = fopen(path, "rb");
	if (!fp) {
		st->saved_errno = errno;
		if (errno == 0) errno = saved_errno;
		MICROJXL__RAISE("open");
	}
	errno = saved_errno;

	source->read_func = microjxl__file_source_read;
	source->seek_func = microjxl__file_source_seek;
	source->free_func = microjxl__file_source_free;
	source->data = fp;
	source->fileoff = 0;
	source->fileoff_limit = ((uint64_t) INT64_MAX < SIZE_MAX ? INT64_MAX : (int64_t) SIZE_MAX);
	return 0;

MICROJXL__ON_ERROR:
	if (fp) fclose(fp);
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__try_read_from_source(
	microjxl__st *st, uint8_t *buf, int64_t minsize, int64_t maxsize, int64_t *size
) {
	microjxl__source_st *source = st->source;
	int64_t read_size = 0;
	int saved_errno = errno;
	errno = 0;
	*size = 0;

	MICROJXL__ASSERT(0 <= minsize && minsize <= maxsize);
	MICROJXL__ASSERT(0 <= source->fileoff && source->fileoff <= source->fileoff_limit);

	// clamp maxsize if fileoff_limit is set
	MICROJXL__ASSERT((uint64_t) source->fileoff_limit <= SIZE_MAX); // so maxsize fits in size_t
	if (maxsize > source->fileoff_limit - source->fileoff) {
		maxsize = source->fileoff_limit - source->fileoff;
		MICROJXL__SHOULD(minsize <= maxsize, "shrt"); // `minsize` bytes can't be read due to virtual EOF
	}

	while (read_size < maxsize) {
		size_t added_size;
		if (MICROJXL_UNLIKELY(source->read_func(
			buf + read_size, source->fileoff, (size_t) (maxsize - read_size), &added_size, source->data
		))) {
			st->saved_errno = errno;
			if (errno == 0) errno = saved_errno;
			MICROJXL__RAISE("read");
		}
		if (added_size == 0) break; // EOF or blocking condition
		MICROJXL__SHOULD(added_size <= (uint64_t) INT64_MAX, "flen");
		read_size += (int64_t) added_size;
		MICROJXL__SHOULD(microjxl__add64(source->fileoff, (int64_t) read_size, &source->fileoff), "flen");
	}

	MICROJXL__SHOULD(read_size >= minsize, "shrt");
	errno = saved_errno;
	*size = read_size;
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__read_from_source(microjxl__st *st, uint8_t *buf, int64_t size) {
	int64_t read_size;
	return microjxl__try_read_from_source(st, buf, size, size, &read_size);
}

MICROJXL__STATIC_RETURNS_ERR microjxl__seek_from_source(microjxl__st *st, int64_t fileoff) {
	microjxl__source_st *source = st->source;

	MICROJXL__ASSERT(fileoff >= 0);
	if (fileoff == source->fileoff) return 0;

	fileoff = microjxl__min64(fileoff, source->fileoff_limit);

	// for the memory source read always have the current fileoff so seek is a no-op
	if (source->seek_func) {
		int saved_errno = errno;
		errno = 0;

		if (MICROJXL_UNLIKELY(source->seek_func(fileoff, source->data))) {
			st->saved_errno = errno;
			if (errno == 0) errno = saved_errno;
			MICROJXL__RAISE("seek");
		}

		errno = saved_errno;
	}

	source->fileoff = fileoff;
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC void microjxl__mem_free_source(microjxl__source_st *source) {
	if (source->free_func) source->free_func(source->data);
	source->read_func = NULL;
	source->seek_func = NULL;
	source->free_func = NULL;
	source->data = NULL;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// container

typedef struct { int64_t codeoff, fileoff; } microjxl__map;

enum microjxl__container_flags {
	// if set, initial jxl & ftyp boxes have been read
	MICROJXL__CONTAINER_CONFIRMED = 1 << 0,

	// currently seen box types, as they have a cardinality and positional requirement
	MICROJXL__SEEN_JXLL = 1 << 1, // at most once, before jxlc/jxlp
	MICROJXL__SEEN_JXLI = 1 << 2, // at most once
	MICROJXL__SEEN_JXLC = 1 << 3, // precludes jxlp, at most once
	MICROJXL__SEEN_JXLP = 1 << 4, // precludes jxlc

	// if set, no more jxlc/jxlp boxes are allowed (and map no longer changes)
	MICROJXL__NO_MORE_CODESTREAM_BOX = 1 << 5,

	// if set, there is an implied entry for `map[nmap]`. this is required when the last
	// codestream box has an unknown length and thus it extends to the (unknown) end of file.
	MICROJXL__IMPLIED_LAST_MAP_ENTRY = 1 << 6,

	// if set, there is no more box past `map[nmap-1]` (or an implied `map[nmap]` if any)
	MICROJXL__NO_MORE_BOX = 1 << 7,
};

typedef struct microjxl__container_st {
	int flags; // bitset of `enum microjxl__container_flags`
	// codestream level from the jxll box (5 or 10; 0 = unspecified when the
	// box is absent, which selects the level 10 superset limits — see
	// microjxl__init_state). Consumed by microjxl__init_state.
	int level;
	// map[0..nmap) encodes two arrays C[i] = map[i].codeoff and F[i] = map[i].fileoff,
	// so that codestream offsets [C[k], C[k+1]) map to file offsets [F[k], F[k] + (C[k+1] - C[k])).
	// all codestream offsets less than the largest C[i] are 1-to-1 mapped to file offsets.
	//
	// the last entry, in particular F[nmap-1], has multiple interpretations.
	// if the mapping is still being built, F[nmap-1] is the start of the next box to be read.
	// if an implicit map entry flag is set, F[nmap] = L and C[nmap] = C[nmap-1] + (L - F[nmap-1])
	// where L is the file length (which is not directly available).
	microjxl__map *map;
	int32_t nmap, map_cap;
	/* JPEG reconstruction (Part 2 §9.10): raw payloads of the jbrd box and
	 * the Exif / `xml ` metadata boxes (Exif content skips its 4-byte TIFF
	 * header indicator; XMP is copied verbatim). NULL = box absent. */
	uint8_t *jbrd; size_t jbrd_size;
	uint8_t *jbrd_alloc; // whole-payload allocation (jbrd points past its own varint preamble)
	uint8_t *exif; size_t exif_size;
	uint8_t *exif_alloc; // whole-payload allocation (exif points 4 bytes in)
	uint8_t *xmp; size_t xmp_size;
} microjxl__container_st;

MICROJXL_ALWAYS_INLINE uint32_t microjxl__u32be(uint8_t *p);
MICROJXL__STATIC_RETURNS_ERR microjxl__box_header(microjxl__st *st, uint32_t *type, int64_t *size);
MICROJXL__STATIC_RETURNS_ERR microjxl__container(microjxl__st *st, int64_t wanted_codeoff);
MICROJXL_STATIC int32_t microjxl__search_codestream_offset(const microjxl__st *st, int64_t codeoff);
MICROJXL__STATIC_RETURNS_ERR microjxl__map_codestream_offset(microjxl__st *st, int64_t codeoff, int64_t *fileoff);
MICROJXL_STATIC void microjxl__mem_free_container(microjxl__container_st *container);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_ALWAYS_INLINE uint32_t microjxl__u32be(uint8_t *p) {
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

// size is < 0 if EOF, or INT64_MAX if the box extends indefinitely until the end of file
MICROJXL__STATIC_RETURNS_ERR microjxl__box_header(microjxl__st *st, uint32_t *type, int64_t *size) {
	uint8_t buf[8];
	uint32_t size32;
	uint64_t size64;
	int64_t headersize;

	MICROJXL__TRY(microjxl__try_read_from_source(st, buf, 0, 8, &headersize));
	if (headersize == 0) {
		*size = -1;
		return 0;
	}
	MICROJXL__SHOULD(headersize == 8, "shrt"); // if not EOF, the full header should have been read

	size32 = microjxl__u32be(buf);
	*type = microjxl__u32be(buf + 4);
	if (size32 == 0) {
		*size = INT64_MAX;
	} else if (size32 == 1) {
		MICROJXL__TRY(microjxl__read_from_source(st, buf, 8));
		size64 = ((uint64_t) microjxl__u32be(buf) << 32) | (uint64_t) microjxl__u32be(buf + 4);
		MICROJXL__SHOULD(size64 >= 16, "boxx");
		MICROJXL__SHOULD(size64 <= INT64_MAX, "flen");
		*size = (int64_t) size64 - 16;
	} else {
		MICROJXL__SHOULD(size32 >= 8, "boxx");
		*size = (int64_t) size32 - 8;
	}

MICROJXL__ON_ERROR:
	return st->err;
}

// scans as many boxes as required to map given codestream offset (i.e. the inclusive limit).
// this is done in the best effort basis, so even after this
// `microjxl__map_codestream_offset(st, wanted_codeoff)` may still fail.
MICROJXL__STATIC_RETURNS_ERR microjxl__container(microjxl__st *st, int64_t wanted_codeoff) {
	static const uint8_t JXL_BOX[12] = { // type `JXL `, value 0D 0A 87 0A
		0x00, 0x00, 0x00, 0x0c, 0x4a, 0x58, 0x4c, 0x20, 0x0d, 0x0a, 0x87, 0x0a,
	}, FTYP_BOX[20] = { // type `ftyp`, brand `jxl `, version 0, only compatible w/ brand `jxl `
		0x00, 0x00, 0x00, 0x14, 0x66, 0x74, 0x79, 0x70, 0x6a, 0x78, 0x6c, 0x20,
		0x00, 0x00, 0x00, 0x00, 0x6a, 0x78, 0x6c, 0x20,
	};

	microjxl__source_st *source = st->source;
	microjxl__container_st *c = st->container;
	uint8_t buf[32];
	/* a box payload is allocated before its bytes are read; if the read
	 * fails (truncated file / mutated box length) the allocation is not
	 * yet owned by the container, so free it on the error path here */
	uint8_t *pending = NULL;

	if (!c->map) {
		c->map_cap = 8;
		c->nmap = 1;
		MICROJXL__TRY_MALLOC(microjxl__map, &c->map, (size_t) c->map_cap);
		c->map[0].codeoff = c->map[0].fileoff = 0; // fileoff will be updated
	}

	// immediately return if given codeoff is already mappable
	if (c->flags & MICROJXL__IMPLIED_LAST_MAP_ENTRY) return 0;
	if (wanted_codeoff < c->map[c->nmap - 1].codeoff) return 0;

	// read the file header (if not yet read) and skip to the next box header
	if (c->flags & MICROJXL__CONTAINER_CONFIRMED) {
		MICROJXL__TRY(microjxl__seek_from_source(st, c->map[c->nmap - 1].fileoff));
	} else {
		MICROJXL__TRY(microjxl__seek_from_source(st, 0));

		MICROJXL__TRY(microjxl__read_from_source(st, buf, 2));
		if (buf[0] == 0xff && buf[1] == 0x0a) { // bare codestream
			c->flags = MICROJXL__CONTAINER_CONFIRMED | MICROJXL__IMPLIED_LAST_MAP_ENTRY;
			return 0;
		}

		MICROJXL__SHOULD(buf[0] == JXL_BOX[0] && buf[1] == JXL_BOX[1], "!jxl");
		MICROJXL__TRY(microjxl__read_from_source(st, buf, sizeof(JXL_BOX) + sizeof(FTYP_BOX) - 2));
		MICROJXL__SHOULD(memcmp(buf, JXL_BOX + 2, sizeof(JXL_BOX) - 2) == 0, "!jxl");
		MICROJXL__SHOULD(memcmp(buf + (sizeof(JXL_BOX) - 2), FTYP_BOX, sizeof(FTYP_BOX)) == 0, "ftyp");
		c->flags |= MICROJXL__CONTAINER_CONFIRMED;
		c->map[0].fileoff = source->fileoff;
	}

	while (wanted_codeoff >= c->map[c->nmap - 1].codeoff) {
		uint32_t type;
		int64_t size;
		int codestream_box = 0;

		MICROJXL__TRY(microjxl__box_header(st, &type, &size));
		if (size < 0) break;

		// TODO the ordering rule for jxll/jxli may change in the future version of 18181-2
		switch (type) {
		case 0x6a786c6c: { // jxll: codestream level
			MICROJXL__SHOULD(!(c->flags & MICROJXL__SEEN_JXLL), "box?");
			c->flags |= MICROJXL__SEEN_JXLL;
			/* 18181-2 C.3: the jxll box content is a one-byte codestream
			 * level (5 or 10). The level selects the decoder capability
			 * limits; record it for microjxl__init_state (the codestream
			 * signature itself does not carry the level). */
			MICROJXL__SHOULD(size >= 1, "jxll");
			MICROJXL__TRY(microjxl__read_from_source(st, buf, 1));
			MICROJXL__SHOULD(buf[0] == 5 || buf[0] == 10, "jxll");
			c->level = buf[0];
			if (size < INT64_MAX) size -= 1;
			break;
		}

		case 0x6a786c69: // jxli: frame index
			MICROJXL__SHOULD(!(c->flags & MICROJXL__SEEN_JXLI), "box?");
			c->flags |= MICROJXL__SEEN_JXLI;
			break;

		case 0x6a627264: { // jbrd: JPEG reconstruction data
			uint8_t *pl;
			MICROJXL__SHOULD(!c->jbrd, "box?"); // at most one
			MICROJXL__SHOULD(size < INT64_MAX && size <= (int64_t) (1 << 30), "jbrd");
			MICROJXL__TRY_MALLOC(uint8_t, &pl, (size_t) size);
			pending = pl;
			MICROJXL__TRY(microjxl__read_from_source(st, pl, (size_t) size));
			pending = NULL;
			/* the payload is the JPEGData bitstream starting at the is_gray
			 * bit (verified against libjxl jpeg::DecodeJPEGData and the
			 * conformance corpus bytes — no leading U64 varint), followed by
			 * a brotli stream after JumpToByteBoundary. */
			c->jbrd = pl;
			c->jbrd_size = (size_t) size;
			c->jbrd_alloc = pl;
			if (size < INT64_MAX) size -= (int64_t) size; // fully consumed
			break;
		}

		case 0x45786966: { // Exif
			uint8_t *pl;
			MICROJXL__SHOULD(!c->exif, "box?");
			MICROJXL__SHOULD(size < INT64_MAX && size <= (int64_t) (1 << 30), "exif");
			if (size > 0) {
				MICROJXL__TRY_MALLOC(uint8_t, &pl, (size_t) size);
				pending = pl;
				MICROJXL__TRY(microjxl__read_from_source(st, pl, (size_t) size));
				pending = NULL;
				/* Part 2 Annex E: the payload's first 4 bytes indicate the
				 * byte order of the TIFF header and are skipped in the
				 * reconstructed JPEG's APP1 (libjxl decode_to_jpeg.cc). */
				if (size >= 4) { c->exif = pl + 4; c->exif_size = (size_t) size - 4; }
				else { c->exif = pl; c->exif_size = (size_t) size; }
				c->exif_alloc = pl;
			}
			if (size < INT64_MAX) size -= (int64_t) size;
			break;
		}

		case 0x786d6c20: { // 'xml ' (XMP)
			uint8_t *pl;
			MICROJXL__SHOULD(!c->xmp, "box?");
			MICROJXL__SHOULD(size < INT64_MAX && size <= (int64_t) (1 << 30), "xmp");
			if (size > 0) {
				MICROJXL__TRY_MALLOC(uint8_t, &pl, (size_t) size);
				pending = pl;
				MICROJXL__TRY(microjxl__read_from_source(st, pl, (size_t) size));
				pending = NULL;
				c->xmp = pl;
				c->xmp_size = (size_t) size;
			}
			if (size < INT64_MAX) size -= (int64_t) size;
			break;
		}

		case 0x6a786c63: // jxlc: single codestream
			MICROJXL__SHOULD(!(c->flags & MICROJXL__NO_MORE_CODESTREAM_BOX), "box?");
			MICROJXL__SHOULD(!(c->flags & (MICROJXL__SEEN_JXLP | MICROJXL__SEEN_JXLC)), "box?");
			c->flags |= MICROJXL__SEEN_JXLC | MICROJXL__NO_MORE_CODESTREAM_BOX;
			codestream_box = 1;
			break;

		case 0x6a786c70: // jxlp: partial codestreams
			MICROJXL__SHOULD(!(c->flags & MICROJXL__NO_MORE_CODESTREAM_BOX), "box?");
			MICROJXL__SHOULD(!(c->flags & MICROJXL__SEEN_JXLC), "box?");
			c->flags |= MICROJXL__SEEN_JXLP;
			codestream_box = 1;
			MICROJXL__SHOULD(size >= 4, "jxlp");
			MICROJXL__TRY(microjxl__read_from_source(st, buf, 4));
			// TODO the partial codestream index is ignored right now
			// spec 18181-2 9.10: the index of the last partial codestream box has
			// its most significant bit set (i.e. >= 2^31); all others are < 2^31.
			if (buf[0] >> 7) c->flags |= MICROJXL__NO_MORE_CODESTREAM_BOX;
			if (size < INT64_MAX) size -= 4;
			break;

		case 0x62726f62: // brob: brotli-compressed box
			MICROJXL__SHOULD(size > 4, "brot"); // Brotli stream is never empty so 4 is also out
			MICROJXL__TRY(microjxl__read_from_source(st, buf, 4));
			type = microjxl__u32be(buf);
			MICROJXL__SHOULD(type != 0x62726f62 /*brob*/ && (type >> 8) != 0x6a786c /*jxl*/, "brot");
			if (size < INT64_MAX) size -= 4;
			break;
		} // other boxes have no additional requirements and are simply skipped

		// this box has an indeterminate size and thus there is no more box following
		if (size == INT64_MAX) {
			if (codestream_box) {
				// the map entry block below is skipped on the break; the source is
				// already positioned right after the box header (and jxlp's 4-byte
				// index), i.e. at the codestream contents, so finalize the map entry
				// here. otherwise F[nmap-1] keeps pointing at the box *header* and
				// the box header bytes would leak into the codestream.
				c->map[c->nmap - 1].fileoff = source->fileoff;
				c->flags |= MICROJXL__IMPLIED_LAST_MAP_ENTRY;
			}
			c->flags |= MICROJXL__NO_MORE_BOX;
			break;
		}

		if (codestream_box) {
			// add a new entry. at this point C[nmap-1] is the first codestream offset in this box
			// and F[nmap-1] points to the beginning of this box, which should be updated to
			// the beginning of the box *contents*.
			MICROJXL__TRY_REALLOC32(microjxl__map, &c->map, c->nmap + 1, &c->map_cap);
			c->map[c->nmap - 1].fileoff = source->fileoff;
			MICROJXL__SHOULD(microjxl__add64(c->map[c->nmap - 1].codeoff, size, &c->map[c->nmap].codeoff), "flen");
			// F[nmap] gets updated in the common case.
			MICROJXL__SHOULD(microjxl__add32(c->nmap, 1, &c->nmap), "flen");
		}

		// always maintains F[nmap-1] to be the beginning of the next box (and seek to that point).
		// we've already read the previous box header, so this should happen even if seek fails.
		MICROJXL__SHOULD(microjxl__add64(source->fileoff, size, &c->map[c->nmap - 1].fileoff), "flen");
		MICROJXL__TRY(microjxl__seek_from_source(st, c->map[c->nmap - 1].fileoff));
	}

	// now the EOF has been reached or the last box had an indeterminate size.
	// EOF condition can be recovered (i.e. we can add more boxes to get it correctly decoded)
	// so it's not a hard error, but we can't recover from an indeterminately sized box.
	if ((c->flags & MICROJXL__NO_MORE_BOX) && !(c->flags & (MICROJXL__SEEN_JXLC | MICROJXL__SEEN_JXLP))) {
		st->cannot_retry = 1;
		MICROJXL__RAISE("shrt");
	}

MICROJXL__ON_ERROR:
	microjxl__mem_free(pending);
	return st->err;
}

// returns i such that codeoff is in [C[i], C[i+1]), or nmap-1 if there is no such map entry
MICROJXL_STATIC int32_t microjxl__search_codestream_offset(const microjxl__st *st, int64_t codeoff) {
	microjxl__map *map = st->container->map;
	int32_t nmap = st->container->nmap, i;
	MICROJXL__ASSERT(map && nmap > 0);
	// TODO use a binary search instead
	for (i = 1; i < nmap; ++i) {
		if (codeoff < map[i].codeoff) break;
	}
	return i - 1;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__map_codestream_offset(microjxl__st *st, int64_t codeoff, int64_t *fileoff) {
	microjxl__map *map = st->container->map;
	int32_t nmap = st->container->nmap, i;

	i = microjxl__search_codestream_offset(st, codeoff);
	if (i < nmap - 1) {
		MICROJXL__ASSERT(codeoff - map[i].codeoff < map[i+1].fileoff - map[i].fileoff);
		*fileoff = map[i].fileoff + (codeoff - map[i].codeoff); // thus this never overflows
	} else if (st->container->flags & MICROJXL__IMPLIED_LAST_MAP_ENTRY) {
		MICROJXL__SHOULD(microjxl__add64(map[nmap-1].fileoff, codeoff - map[nmap-1].codeoff, fileoff), "flen");
	} else if (st->container->flags & MICROJXL__NO_MORE_CODESTREAM_BOX) {
		// TODO is this valid to do? microjxl__end_of_frame depends on this.
		if (codeoff == map[nmap-1].codeoff) {
			*fileoff = map[nmap-1].fileoff;
		} else {
			st->cannot_retry = 1;
			MICROJXL__RAISE("shrt");
		}
	} else {
		MICROJXL__RAISE("shrt");
	}

MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC void microjxl__mem_free_container(microjxl__container_st *container) {
	microjxl__mem_free(container->map);
	container->map = NULL;
	container->nmap = container->map_cap = 0;
	/* jbrd/Exif/'xml ' box payloads (JPEG reconstruction). jbrd_alloc /
	 * exif_alloc own the whole allocations; jbrd / exif may point past a
	 * small preamble inside them, so only the *alloc pointers are freed. */
	microjxl__mem_free(container->jbrd_alloc);
	container->jbrd = NULL; container->jbrd_size = 0;
	container->jbrd_alloc = NULL;
	microjxl__mem_free(container->exif_alloc);
	container->exif = NULL; container->exif_size = 0;
	container->exif_alloc = NULL;
	microjxl__mem_free(container->xmp);
	container->xmp = NULL; container->xmp_size = 0;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// backing buffer

typedef struct microjxl__buffer_st {
	uint8_t *buf;
	int64_t size, capacity;

	int64_t next_codeoff; // the codestream offset right past the backing buffer (i.e. `buf[size]`)
	int64_t codeoff_limit; // codestream offset can't exceed this; used for per-section decoding

	microjxl__bits_st checkpoint; // the earliest point that the parser can ever backtrack
} microjxl__buffer_st;

MICROJXL__STATIC_RETURNS_ERR microjxl__init_buffer(microjxl__st *st, int64_t codeoff, int64_t codeoff_limit);
MICROJXL__STATIC_RETURNS_ERR microjxl__refill_buffer(microjxl__st *st);
MICROJXL__STATIC_RETURNS_ERR microjxl__seek_buffer(microjxl__st *st, int64_t codeoff);
MICROJXL_STATIC int64_t microjxl__codestream_offset(const microjxl__st *st);
MICROJXL_MAYBE_UNUSED MICROJXL_STATIC int64_t microjxl__bits_read(const microjxl__st *st);
MICROJXL_STATIC void microjxl__mem_free_buffer(microjxl__buffer_st *buffer);

#ifdef MICROJXL_IMPLEMENTATION

#define MICROJXL__INITIAL_BUFSIZE 0x10000

MICROJXL__STATIC_RETURNS_ERR microjxl__init_buffer(microjxl__st *st, int64_t codeoff, int64_t codeoff_limit) {
	microjxl__bits_st *bits = &st->bits, *checkpoint = &st->buffer->checkpoint;
	microjxl__buffer_st *buffer = st->buffer;

	MICROJXL__ASSERT(!buffer->buf);
	MICROJXL__TRY_MALLOC(uint8_t, &buffer->buf, MICROJXL__INITIAL_BUFSIZE);
	bits->ptr = bits->end = buffer->buf;
	buffer->size = 0;
	buffer->capacity = MICROJXL__INITIAL_BUFSIZE;
	buffer->next_codeoff = codeoff;
	buffer->codeoff_limit = codeoff_limit;
	bits->bits = 0;
	bits->nbits = 0;
	*checkpoint = *bits;
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__refill_buffer(microjxl__st *st) {
	microjxl__bits_st *bits = &st->bits, *checkpoint = &st->buffer->checkpoint;
	microjxl__buffer_st *buffer = st->buffer;
	microjxl__container_st *container = st->container;
	int64_t available, wanted_codeoff;
	int32_t i;

	MICROJXL__ASSERT(MICROJXL__INBOUNDS(bits->ptr, buffer->buf, buffer->size));
	MICROJXL__ASSERT(MICROJXL__INBOUNDS(checkpoint->ptr, buffer->buf, buffer->size));
	MICROJXL__ASSERT(checkpoint->ptr <= bits->ptr);

	// trim the committed portion from the backing buffer
	if (checkpoint->ptr > buffer->buf) {
		int64_t committed_size = (int64_t) (checkpoint->ptr - buffer->buf);
		MICROJXL__ASSERT(committed_size <= buffer->size); // so committed_size can't overflow
		// this also can't overflow, because buffer->size never exceeds SIZE_MAX
		memmove(buffer->buf, checkpoint->ptr, (size_t) (buffer->size - committed_size));
		buffer->size -= committed_size;
		bits->ptr -= committed_size;
		bits->end -= committed_size;
		checkpoint->ptr = buffer->buf;
	}

	// if there is no room left in the backing buffer, it's time to grow it
	if (buffer->size == buffer->capacity) {
		int64_t newcap = microjxl__clamp_add64(buffer->capacity, buffer->capacity);
		ptrdiff_t relptr = bits->ptr - buffer->buf;
		MICROJXL__TRY_REALLOC64(uint8_t, &buffer->buf, newcap, &buffer->capacity);
		bits->ptr = buffer->buf + relptr;
		checkpoint->ptr = buffer->buf;
	}

	wanted_codeoff = microjxl__min64(buffer->codeoff_limit,
		microjxl__clamp_add64(buffer->next_codeoff, buffer->capacity - buffer->size));
	available = wanted_codeoff - buffer->next_codeoff;
	--wanted_codeoff; // ensure that this is inclusive, i.e. the last byte offset *allowed*

	// do the initial mapping if no map is available
	if (!container->map) MICROJXL__TRY(microjxl__container(st, wanted_codeoff));

	i = microjxl__search_codestream_offset(st, buffer->next_codeoff);
	while (available > 0) {
		microjxl__map *map = container->map;
		int32_t nmap = container->nmap;
		int64_t fileoff, readable_size, read_size;

		if (i < nmap - 1) {
			int64_t box_size = map[i+1].codeoff - map[i].codeoff;
			MICROJXL__ASSERT(box_size > 0);
			readable_size = microjxl__min64(available, map[i+1].codeoff - buffer->next_codeoff);
			MICROJXL__ASSERT(buffer->next_codeoff - map[i].codeoff < map[i+1].fileoff - map[i].fileoff);
			fileoff = map[i].fileoff + (buffer->next_codeoff - map[i].codeoff); // thus can't overflow
		} else if (container->flags & MICROJXL__IMPLIED_LAST_MAP_ENTRY) {
			readable_size = available;
			MICROJXL__SHOULD(
				microjxl__add64(map[i].fileoff, buffer->next_codeoff - map[nmap-1].codeoff, &fileoff),
				"flen");
		} else {
			// we have reached past the last mapped box, but there may be more boxes to map
			MICROJXL__TRY(microjxl__container(st, wanted_codeoff));
			if (nmap == container->nmap && !(container->flags & MICROJXL__IMPLIED_LAST_MAP_ENTRY)) {
				break; // no additional box mapped, nothing can be done
			}
			continue;
		}
		MICROJXL__ASSERT(readable_size > 0);

		MICROJXL__TRY(microjxl__seek_from_source(st, fileoff));
		MICROJXL__TRY(microjxl__try_read_from_source(st, buffer->buf + buffer->size, 0, readable_size, &read_size));
		if (read_size == 0) break; // EOF or blocking condition, can't continue

		buffer->size += read_size;
		MICROJXL__SHOULD(microjxl__add64(buffer->next_codeoff, read_size, &buffer->next_codeoff), "flen");
		bits->end = checkpoint->end = buffer->buf + buffer->size;
		available -= read_size;
		if (read_size == readable_size) ++i; // try again if read is somehow incomplete
	}

MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__seek_buffer(microjxl__st *st, int64_t codeoff) {
	int64_t reusable_size = st->buffer->next_codeoff - codeoff, fileoff;
	st->bits.bits = 0;
	st->bits.nbits = 0;
	if (0 < reusable_size && reusable_size <= st->buffer->size) {
		st->bits.ptr = st->buffer->buf + (st->buffer->size - reusable_size);
		st->bits.end = st->buffer->buf + st->buffer->size;
	} else {
		st->bits.ptr = st->bits.end = st->buffer->buf;
		st->buffer->size = 0;
		st->buffer->next_codeoff = codeoff;
		MICROJXL__TRY(microjxl__map_codestream_offset(st, codeoff, &fileoff));
		MICROJXL__TRY(microjxl__seek_from_source(st, fileoff));
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC int64_t microjxl__codestream_offset(const microjxl__st *st) {
	MICROJXL__ASSERT(st->bits.nbits % 8 == 0);
	return st->buffer->next_codeoff - st->buffer->size + (st->bits.ptr - st->buffer->buf) - st->bits.nbits / 8;
}

// diagnostic only, doesn't check for overflow or anything
MICROJXL_MAYBE_UNUSED MICROJXL_STATIC int64_t microjxl__bits_read(const microjxl__st *st) {
	int32_t nbytes = microjxl__ceil_div32(st->bits.nbits, 8), nbits = 8 * nbytes - st->bits.nbits;
	// the codestream offset for the byte that contains the first bit to read
	int64_t codeoff = st->buffer->next_codeoff - st->buffer->size + (st->bits.ptr - st->buffer->buf) - nbytes;
	microjxl__map map = st->container->map[microjxl__search_codestream_offset(st, codeoff)];
	return (map.fileoff + (codeoff - map.codeoff)) * 8 + nbits;
}

MICROJXL_STATIC void microjxl__mem_free_buffer(microjxl__buffer_st *buffer) {
	microjxl__mem_free(buffer->buf);
	buffer->buf = NULL;
	buffer->size = buffer->capacity = 0;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// bitstream

MICROJXL__STATIC_RETURNS_ERR microjxl__always_refill(microjxl__st *st, int32_t n);
// ensure st->bits.nbits is at least n; otherwise pull as many bytes as possible into st->bits.bits
#define microjxl__refill(st, n) (MICROJXL_UNLIKELY(st->bits.nbits < (n)) ? microjxl__always_refill(st, n) : st->err)

MICROJXL__INLINE_RETURNS_ERR microjxl__zero_pad_to_byte(microjxl__st *st);
MICROJXL__STATIC_RETURNS_ERR microjxl__skip(microjxl__st *st, int64_t n);

MICROJXL_INLINE int32_t microjxl__u(microjxl__st *st, int32_t n);
MICROJXL_INLINE int64_t microjxl__64u(microjxl__st *st, int32_t n);
MICROJXL_INLINE int32_t microjxl__u32(
	microjxl__st *st,
	int32_t o0, int32_t n0, int32_t o1, int32_t n1,
	int32_t o2, int32_t n2, int32_t o3, int32_t n3
);
MICROJXL_INLINE int64_t microjxl__64u32(
	microjxl__st *st,
	int32_t o0, int32_t n0, int32_t o1, int32_t n1,
	int32_t o2, int32_t n2, int32_t o3, int32_t n3
);
MICROJXL_STATIC uint64_t microjxl__u64(microjxl__st *st);
MICROJXL_INLINE int32_t microjxl__enum(microjxl__st *st);
MICROJXL_INLINE float microjxl__f16(microjxl__st *st);
MICROJXL_INLINE int32_t microjxl__u8(microjxl__st *st);
MICROJXL_INLINE int32_t microjxl__at_most(microjxl__st *st, int32_t max);
MICROJXL__STATIC_RETURNS_ERR microjxl__no_more_bytes(microjxl__st *st);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__always_refill(microjxl__st *st, int32_t n) {
	static const int32_t NBITS = 64;
	microjxl__bits_st *bits = &st->bits;

	MICROJXL__ASSERT(0 <= n && n < NBITS);
	while (1) {
		int32_t consumed = (NBITS - bits->nbits) >> 3;
		if (MICROJXL_LIKELY(bits->end - bits->ptr >= consumed)) {
			// fast case: consume `consumed` bytes from the logical buffer
			MICROJXL__ASSERT(bits->nbits <= NBITS - 8);
			do {
				bits->bits |= (uint64_t) *bits->ptr++ << bits->nbits;
				bits->nbits += 8;
			} while (bits->nbits <= NBITS - 8);
			break;
		}

		// slow case: the logical buffer has been exhausted, try to refill the backing buffer
		while (bits->ptr < bits->end) {
			bits->bits |= (uint64_t) *bits->ptr++ << bits->nbits;
			bits->nbits += 8;
		}
		if (bits->nbits > NBITS - 8) break;

		MICROJXL__SHOULD(st->buffer, "shrt");
		MICROJXL__TRY(microjxl__refill_buffer(st));
		if (bits->end == bits->ptr) { // no possibility to read more bits
			if (bits->nbits >= n) break;
			MICROJXL__RAISE("shrt");
		}
		// otherwise now we have possibly more bits to refill, try again
	}

MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__INLINE_RETURNS_ERR microjxl__zero_pad_to_byte(microjxl__st *st) {
	int32_t n = st->bits.nbits & 7;
	if (st->bits.bits & ((1u << n) - 1)) return MICROJXL__ERR("pad0");
	st->bits.bits >>= n;
	st->bits.nbits -= n;
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__skip(microjxl__st *st, int64_t n) {
	microjxl__bits_st *bits = &st->bits;
	int64_t bytes;
	if (bits->nbits >= n) {
		bits->bits >>= (int32_t) n;
		bits->nbits -= (int32_t) n;
	} else {
		n -= bits->nbits;
		bits->bits = 0;
		bits->nbits = 0;
	}
	bytes = n >> 3;
	// TODO honor containers
	if (bits->end - bits->ptr < (int64_t) bytes) return MICROJXL__ERR("shrt");
	bits->ptr += bytes;
	n &= 7;
	if (microjxl__refill(st, (int32_t) n)) return st->err;
	bits->bits >>= (int32_t) n;
	bits->nbits -= (int32_t) n;
	return st->err;
}

MICROJXL_INLINE int32_t microjxl__u(microjxl__st *st, int32_t n) {
	int32_t ret;
	MICROJXL__ASSERT(0 <= n && n <= 31);
	if (microjxl__refill(st, n)) return 0;
	ret = (int32_t) (st->bits.bits & ((1u << n) - 1));
	st->bits.bits >>= n;
	st->bits.nbits -= n;
	return ret;
}

MICROJXL_INLINE int64_t microjxl__64u(microjxl__st *st, int32_t n) {
	int64_t ret;
	MICROJXL__ASSERT(0 <= n && n <= 63);
	if (microjxl__refill(st, n)) return 0;
	ret = (int64_t) (st->bits.bits & (((uint64_t) 1u << n) - 1));
	st->bits.bits >>= n;
	st->bits.nbits -= n;
	return ret;
}

MICROJXL_INLINE int32_t microjxl__u32(
	microjxl__st *st,
	int32_t o0, int32_t n0, int32_t o1, int32_t n1,
	int32_t o2, int32_t n2, int32_t o3, int32_t n3
) {
	const int32_t o[4] = { o0, o1, o2, o3 };
	const int32_t n[4] = { n0, n1, n2, n3 };
	int32_t sel;
	MICROJXL__ASSERT(0 <= n0 && n0 <= 30 && o0 <= 0x7fffffff - (1 << n0));
	MICROJXL__ASSERT(0 <= n1 && n1 <= 30 && o1 <= 0x7fffffff - (1 << n1));
	MICROJXL__ASSERT(0 <= n2 && n2 <= 30 && o2 <= 0x7fffffff - (1 << n2));
	MICROJXL__ASSERT(0 <= n3 && n3 <= 30 && o3 <= 0x7fffffff - (1 << n3));
	sel = microjxl__u(st, 2);
	return microjxl__u(st, n[sel]) + o[sel];
}

MICROJXL_INLINE int64_t microjxl__64u32(
	microjxl__st *st,
	int32_t o0, int32_t n0, int32_t o1, int32_t n1,
	int32_t o2, int32_t n2, int32_t o3, int32_t n3
) {
	const int32_t o[4] = { o0, o1, o2, o3 };
	const int32_t n[4] = { n0, n1, n2, n3 };
	int32_t sel;
	MICROJXL__ASSERT(0 <= n0 && n0 <= 62);
	MICROJXL__ASSERT(0 <= n1 && n1 <= 62);
	MICROJXL__ASSERT(0 <= n2 && n2 <= 62);
	MICROJXL__ASSERT(0 <= n3 && n3 <= 62);
	sel = microjxl__u(st, 2);
	return (microjxl__64u(st, n[sel]) + (int64_t) o[sel]) & (int64_t) 0xffffffff;
}

MICROJXL_STATIC uint64_t microjxl__u64(microjxl__st *st) {
	int32_t sel = microjxl__u(st, 2), shift;
	uint64_t ret = (uint64_t) microjxl__u(st, sel * 4);
	if (sel < 3) {
		ret += 17u >> (8 - sel * 4);
	} else {
		for (shift = 12; shift < 64 && microjxl__u(st, 1); shift += 8) {
			ret |= (uint64_t) microjxl__u(st, shift < 56 ? 8 : 64 - shift) << shift;
		}
	}
	return ret;
}

MICROJXL_INLINE int32_t microjxl__enum(microjxl__st *st) {
	int32_t ret = microjxl__u32(st, 0, 0, 1, 0, 2, 4, 18, 6);
	// the spec says it should be 64, but the largest enum value in use is 18 (kHLG);
	// we have to reject unknown enum values anyway so we use a smaller limit to avoid overflow
	if (ret >= 31) return MICROJXL__ERR("enum"), 0;
	return ret;
}

MICROJXL_INLINE float microjxl__f16(microjxl__st *st) {
	int32_t bits = microjxl__u(st, 16);
	int32_t biased_exp = (bits >> 10) & 0x1f;
	if (biased_exp == 31) return MICROJXL__ERR("!fin"), 0.0f;
	return (bits >> 15 ? -1 : 1) * ldexpf((float) ((bits & 0x3ff) | (biased_exp > 0 ? 0x400 : 0)), biased_exp - 25);
}

MICROJXL_INLINE int32_t microjxl__u8(microjxl__st *st) { // ANS distribution decoding only
	if (microjxl__u(st, 1)) {
		int32_t n = microjxl__u(st, 3);
		return microjxl__u(st, n) + (1 << n);
	} else {
		return 0;
	}
}

// equivalent to u(ceil(log2(max + 1))), decodes [0, max] with the minimal number of bits
MICROJXL_INLINE int32_t microjxl__at_most(microjxl__st *st, int32_t max) {
	int32_t v = max > 0 ? microjxl__u(st, microjxl__ceil_lg32((uint32_t) max + 1)) : 0;
	if (v > max) return MICROJXL__ERR("rnge"), 0;
	return v;
}

// ensures that we have reached the end of file or advertised section with proper padding
MICROJXL__STATIC_RETURNS_ERR microjxl__no_more_bytes(microjxl__st *st) {
	MICROJXL__TRY(microjxl__zero_pad_to_byte(st));
	MICROJXL__SHOULD(st->bits.nbits == 0 && st->bits.ptr == st->bits.end, "excs");
MICROJXL__ON_ERROR:
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// prefix code

MICROJXL__STATIC_RETURNS_ERR microjxl__prefix_code_tree(
	microjxl__st *st, int32_t l2size, int32_t *out_fast_len, int32_t *out_max_len, int32_t **out_table
);
MICROJXL_INLINE int32_t microjxl__prefix_code(microjxl__st *st, int32_t fast_len, int32_t max_len, const int32_t *table);

#ifdef MICROJXL_IMPLEMENTATION

// a prefix code tree is represented by max_len (max code length), fast_len (explained below),
// and an int32_t table either statically or dynamically constructed.
// table[0] .. table[(1 << fast_len) - 1] are a lookup table for first fast_len bits.
// each entry is either a direct entry (positive),
// or an index to the first overflow entry (negative, the actual index is -table[i]).
//
// subsequent overflow entries are used for codes with the length > fast_len;
// the decoder reads overflow entries in the order, stopping at the first match.
// the last overflow entry is implicit so the table is constructed to ensure the match.
//
// a direct or overflow entry format:
// - bits 0..3: codeword length - fast_len
// - bits 4..15: codeword, skipping first fast_len bits, ordered like st->bits.bits (overflow only)
// - bits 16..30: corresponding alphabet

enum { MICROJXL__MAX_TYPICAL_FAST_LEN = 7 }; // limit fast_len for typical cases
enum { MICROJXL__MAX_TABLE_GROWTH = 2 }; // we can afford 2x the table size if beneficial though

// read a prefix code tree, as specified in RFC 7932 section 3
MICROJXL__STATIC_RETURNS_ERR microjxl__prefix_code_tree(
	microjxl__st *st, int32_t l2size, int32_t *out_fast_len, int32_t *out_max_len, int32_t **out_table
) {
	static const uint8_t REV5[32] = {
		0, 16, 8, 24, 4, 20, 12, 28, 2, 18, 10, 26, 6, 22, 14, 30,
		1, 17, 9, 25, 5, 21, 13, 29, 3, 19, 11, 27, 7, 23, 15, 31,
	};

	// for ordinary cases we have three different prefix codes:
	// layer 0 (fixed): up to 4 bits, decoding into 0..5, used L1SIZE = 18 times
	// layer 1: up to 5 bits, decoding into 0..17, used l2size times
	// layer 2: up to 15 bits, decoding into 0..l2size-1
	enum { L1SIZE = 18, L0MAXLEN = 4, L1MAXLEN = 5, L2MAXLEN = 15 };
	enum { L1CODESUM = 1 << L1MAXLEN, L2CODESUM = 1 << L2MAXLEN };
	static const int32_t L0TABLE[1 << L0MAXLEN] = {
		0x00002, 0x40002, 0x30002, 0x20003, 0x00002, 0x40002, 0x30002, 0x10004,
		0x00002, 0x40002, 0x30002, 0x20003, 0x00002, 0x40002, 0x30002, 0x50004,
	};
	static const uint8_t L1ZIGZAG[L1SIZE] = {1,2,3,4,0,5,17,6,16,7,8,9,10,11,12,13,14,15};

	int32_t l1lengths[L1SIZE] = {0}, *l2lengths = NULL;
	int32_t l1counts[L1MAXLEN + 1] = {0}, l2counts[L2MAXLEN + 1] = {0};
	int32_t l1starts[L1MAXLEN + 1], l2starts[L2MAXLEN + 1], l2overflows[L2MAXLEN + 1];
	int32_t l1table[1 << L1MAXLEN] = {0}, *l2table = NULL;
	int32_t total, code, hskip, fast_len, i, j;

	MICROJXL__ASSERT(l2size > 0 && l2size <= 0x8000);
	if (l2size == 1) { // SPEC missing this case
		*out_fast_len = *out_max_len = 0;
		MICROJXL__TRY_MALLOC(int32_t, out_table, 1);
		(*out_table)[0] = 0;
		return 0;
	}

	hskip = microjxl__u(st, 2);
	if (hskip == 1) { // simple prefix codes (section 3.4)
		static const struct { int8_t maxlen, sortfrom, sortto, len[8], symref[8]; } TEMPLATES[5] = {
			{ 3, 2, 4, {1,2,1,3,1,2,1,3}, {0,1,0,2,0,1,0,3} }, // NSYM=4 tree-select 1 (1233)
			{ 0, 0, 0, {0}, {0} },                             // NSYM=1 (0)
			{ 1, 0, 2, {1,1}, {0,1} },                         // NSYM=2 (11)
			{ 2, 1, 3, {1,2,1,2}, {0,1,0,2} },                 // NSYM=3 (122)
			{ 2, 0, 4, {2,2,2,2}, {0,2,1,3} },                 // NSYM=4 tree-select 0 (2222)
		};
		int32_t nsym = microjxl__u(st, 2) + 1, syms[4], tmp;
		for (i = 0; i < nsym; ++i) {
			syms[i] = microjxl__at_most(st, l2size - 1);
			for (j = 0; j < i; ++j) MICROJXL__SHOULD(syms[i] != syms[j], "hufd");
		}
		if (nsym == 4 && microjxl__u(st, 1)) nsym = 0; // tree-select
		MICROJXL__RAISE_DELAYED();

		// symbols of the equal length have to be sorted
		for (i = TEMPLATES[nsym].sortfrom + 1; i < TEMPLATES[nsym].sortto; ++i) {
			for (j = i; j > TEMPLATES[nsym].sortfrom && syms[j - 1] > syms[j]; --j) {
				tmp = syms[j - 1];
				syms[j - 1] = syms[j];
				syms[j] = tmp;
			}
		}

		*out_fast_len = *out_max_len = TEMPLATES[nsym].maxlen;
		MICROJXL__TRY_MALLOC(int32_t, out_table, 1u << *out_max_len);
		for (i = 0; i < (1 << *out_max_len); ++i) {
			(*out_table)[i] = (syms[TEMPLATES[nsym].symref[i]] << 16) | (int32_t) TEMPLATES[nsym].len[i];
		}
		return 0;
	}

	// complex prefix codes (section 3.5): read layer 1 code lengths using the layer 0 code
	total = 0;
	for (i = l1counts[0] = hskip; i < L1SIZE && total < L1CODESUM; ++i) {
		l1lengths[L1ZIGZAG[i]] = code = microjxl__prefix_code(st, L0MAXLEN, L0MAXLEN, L0TABLE);
		++l1counts[code];
		if (code) total += L1CODESUM >> code;
	}
	MICROJXL__SHOULD((total == L1CODESUM || l1counts[0] == i - 1) && l1counts[0] != i, "hufd");

	// construct the layer 1 tree
	if (l1counts[0] == i - 1) { // degenerate: exactly one nonzero code length (RFC 7932
		// section 3.5 / brotli "num_codes == 1"): the L1 code is a 0-bit code that
		// always decodes to that single symbol (libjxl huffman_table.cc "special
		// case code with only one value"), regardless of its assigned length
		for (i = 0; l1lengths[i] == 0; ++i); // the single coded symbol; SHOULD terminates
		for (code = 0; code < L1CODESUM; ++code) l1table[code] = i << 16;
	} else {
		l1starts[1] = 0;
		for (i = 2; i <= L1MAXLEN; ++i) {
			l1starts[i] = l1starts[i - 1] + (l1counts[i - 1] << (L1MAXLEN - (i - 1)));
		}
		for (i = 0; i < L1SIZE; ++i) {
			int32_t n = l1lengths[i], *start = &l1starts[n];
			if (n == 0) continue;
			for (code = (int32_t) REV5[*start]; code < L1CODESUM; code += 1 << n) {
				l1table[code] = (i << 16) | n;
			}
			*start += L1CODESUM >> n;
		}
	}

	{ // read layer 2 code lengths using the layer 1 code
		int32_t prev = 8, rep, prev_rep = 0; // prev_rep: prev repeat count of 16(pos)/17(neg) so far
		MICROJXL__TRY_CALLOC(int32_t, &l2lengths, (size_t) l2size);
		for (i = total = 0; i < l2size && total < L2CODESUM; ) {
			code = microjxl__prefix_code(st, L1MAXLEN, L1MAXLEN, l1table);
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_HUFF")) fprintf(stderr, "[microjxl-huff] l2[%d]=%d bitpos=%lld\n", i, code, (long long) microjxl__bits_read(st));
#endif
			if (code < 16) {
				l2lengths[i++] = code;
				++l2counts[code];
				if (code) {
					total += L2CODESUM >> code;
					prev = code;
				}
				prev_rep = 0;
			} else if (code == 16) { // repeat non-zero 3+u(2) times
				// instead of keeping the current repeat count, we calculate a difference
				// between the previous and current repeat count and directly apply the delta
				if (prev_rep < 0) prev_rep = 0;
				rep = (prev_rep > 0 ? 4 * prev_rep - 5 : 3) + microjxl__u(st, 2);
				MICROJXL__SHOULD(i + (rep - prev_rep) <= l2size, "hufd");
				total += (L2CODESUM * (rep - prev_rep)) >> prev;
				l2counts[prev] += rep - prev_rep;
				for (; prev_rep < rep; ++prev_rep) l2lengths[i++] = prev;
			} else { // code == 17: repeat zero 3+u(3) times
				if (prev_rep > 0) prev_rep = 0;
				rep = (prev_rep < 0 ? 8 * prev_rep + 13 : -3) - microjxl__u(st, 3);
				MICROJXL__SHOULD(i + (prev_rep - rep) <= l2size, "hufd");
				for (; prev_rep > rep; --prev_rep) l2lengths[i++] = 0;
			}
			MICROJXL__RAISE_DELAYED();
		}
		MICROJXL__SHOULD(total == L2CODESUM, "hufd");
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_HUFF")) { fprintf(stderr, "[microjxl-huff] l2lengths: "); for (i = 0; i < l2size; ++i) fprintf(stderr, "%d ", l2lengths[i]); fprintf(stderr, "\n"); }
#endif
	}

	// determine the layer 2 lookup table size
	l2starts[1] = 0;
	*out_max_len = 1;
	for (i = 2; i <= L2MAXLEN; ++i) {
		l2starts[i] = l2starts[i - 1] + (l2counts[i - 1] << (L2MAXLEN - (i - 1)));
		if (l2counts[i]) *out_max_len = i;
	}
	if (*out_max_len <= MICROJXL__MAX_TYPICAL_FAST_LEN) {
		fast_len = *out_max_len;
		MICROJXL__TRY_MALLOC(int32_t, &l2table, 1u << fast_len);
	} else {
		// if the distribution is flat enough the max fast_len might be slow
		// because most LUT entries will be overflow refs so we will hit slow paths for most cases.
		// we therefore calculate the table size with the max fast_len,
		// then find the largest fast_len within the specified table growth factor.
		int32_t size, size_limit, size_used;
		fast_len = MICROJXL__MAX_TYPICAL_FAST_LEN;
		size = 1 << fast_len;
		for (i = fast_len + 1; i <= *out_max_len; ++i) size += l2counts[i];
		size_used = size;
		size_limit = size * MICROJXL__MAX_TABLE_GROWTH;
		for (i = fast_len + 1; i <= *out_max_len; ++i) {
			size = size + (1 << i) - l2counts[i];
			if (size <= size_limit) {
				size_used = size;
				fast_len = i;
			}
		}
		l2overflows[fast_len + 1] = 1 << fast_len;
		for (i = fast_len + 2; i <= *out_max_len; ++i) l2overflows[i] = l2overflows[i - 1] + l2counts[i - 1];
		MICROJXL__TRY_MALLOC(int32_t, &l2table, (size_t) (size_used + 1));
		// this entry should be unreachable, but should work as a stopper if there happens to be a logic bug
		l2table[size_used] = 0;
	}

	// fill the layer 2 table
	for (i = 0; i < l2size; ++i) {
		int32_t n = l2lengths[i], *start = &l2starts[n];
		if (n == 0) continue;
		code = ((int32_t) REV5[*start & 31] << 10) |
			((int32_t) REV5[*start >> 5 & 31] << 5) |
			((int32_t) REV5[*start >> 10]);
		if (n <= fast_len) {
			for (; code < (1 << fast_len); code += 1 << n) l2table[code] = (i << 16) | n;
			*start += L2CODESUM >> n;
		} else {
			// there should be exactly one code which is a LUT-covered prefix plus all zeroes;
			// in the canonical Huffman tree that code would be in the first overflow entry
			if ((code >> fast_len) == 0) l2table[code] = -l2overflows[n];
			*start += L2CODESUM >> n;
			l2table[l2overflows[n]++] = (i << 16) | (code >> fast_len << 4) | (n - fast_len);
		}
	}

	*out_fast_len = fast_len;
	*out_table = l2table;
	microjxl__mem_free(l2lengths);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(l2lengths);
	microjxl__mem_free(l2table);
	return st->err;
}

MICROJXL_STATIC int32_t microjxl__match_overflow(microjxl__st *st, int32_t fast_len, const int32_t *table) {
	int32_t entry, code, code_len;
	st->bits.nbits -= fast_len;
	st->bits.bits >>= fast_len;
	do {
		entry = *table++;
		code = (entry >> 4) & 0xfff;
		code_len = entry & 15;
	} while (code != (int32_t) (st->bits.bits & ((1u << code_len) - 1)));
	return entry;
}

MICROJXL_INLINE int32_t microjxl__prefix_code(microjxl__st *st, int32_t fast_len, int32_t max_len, const int32_t *table) {
	int32_t entry, code_len;
	// this is not `microjxl__refill(st, max_len)` because it should be able to handle codes
	// at the very end of file or section and shorter than max_len bits; in that case
	// the bit buffer will correctly contain a short code padded with zeroes.
	if (st->bits.nbits < max_len && microjxl__always_refill(st, 0)) return 0;
	entry = table[st->bits.bits & ((1u << fast_len) - 1)];
	if (entry < 0 && fast_len < max_len) entry = microjxl__match_overflow(st, fast_len, table - entry);
	code_len = entry & 15;
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_PREFIX")) fprintf(stderr, "[microjxl-pfx] fl=%d ml=%d entry=%d cl=%d sym=%d bits=%08x nbits=%d bitpos=%lld\n", fast_len, max_len, entry, code_len, entry >> 16, (unsigned) st->bits.bits, st->bits.nbits, (long long) microjxl__bits_read(st));
#endif
	st->bits.nbits -= code_len;
	st->bits.bits >>= code_len;
	if (st->bits.nbits < 0) { // too many bits read from the bit buffer
		st->bits.nbits = 0;
		MICROJXL__ASSERT(st->bits.bits == 0);
		MICROJXL__ERR("shrt");
	}
	return entry >> 16;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// hybrid integer encoding

// token < 2^split_exp is interpreted as is.
// otherwise (token - 2^split_exp) is split into NNHHHLLL where config determines H/L lengths.
// then MMMMM = u(NN + split_exp - H/L lengths) is read; the decoded value is 1HHHMMMMMLLL.
typedef struct {
	int8_t split_exp; // [0, 15]
	int8_t msb_in_token, lsb_in_token; // msb_in_token + lsb_in_token <= split_exp
} microjxl__hybrid_int_config;

MICROJXL__STATIC_RETURNS_ERR microjxl__read_hybrid_int_config(
	microjxl__st *st, int32_t log_alpha_size, microjxl__hybrid_int_config *out
);
MICROJXL_INLINE int32_t microjxl__hybrid_int(microjxl__st *st, int32_t token, microjxl__hybrid_int_config config);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__read_hybrid_int_config(
	microjxl__st *st, int32_t log_alpha_size, microjxl__hybrid_int_config *out
) {
	MICROJXL__ASSERT(log_alpha_size <= 15);
	/* The split_exponent field is a raw bit field: any value that fits in
	 * ceil_log2(log_alpha_size+1) bits is legal, including values greater
	 * than log_alpha_size (libjxl DecodeUintConfig reads it without
	 * validation and only checks msb/lsb afterwards). The msb/lsb reads
	 * below are bounded and validate like libjxl. */
	out->split_exp = (int8_t) microjxl__u(st, microjxl__ceil_lg32((uint32_t) log_alpha_size + 1));
	if (out->split_exp != log_alpha_size) {
		out->msb_in_token = (int8_t) microjxl__at_most(st, out->split_exp);
		out->lsb_in_token = (int8_t) microjxl__at_most(st, out->split_exp - out->msb_in_token);
	} else {
		out->msb_in_token = out->lsb_in_token = 0;
	}
	return st->err;
}

MICROJXL_INLINE int32_t microjxl__hybrid_int(microjxl__st *st, int32_t token, microjxl__hybrid_int_config config) {
	int32_t midbits, lo, mid, hi, top, bits_in_token, split = 1 << config.split_exp;
	if (token < split) return token;
	bits_in_token = config.msb_in_token + config.lsb_in_token;
	midbits = config.split_exp - bits_in_token + ((token - split) >> bits_in_token);
	midbits &= 31; // SPEC: result < 2^32; libjxl wraps oversized midbits instead of erroring
	mid = microjxl__u(st, midbits);
	top = 1 << config.msb_in_token;
	lo = token & ((1 << config.lsb_in_token) - 1);
	hi = (token >> config.lsb_in_token) & (top - 1);
	// compute in 64 bits and truncate to 32 (libjxl casts its uint32_t result to
	// the pixel type); 32-bit float samples legitimately produce large tokens
	return (int32_t)(uint32_t)((((uint64_t)(top | hi) << midbits | (uint32_t) mid) << config.lsb_in_token) | (uint32_t) lo);
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// rANS alias table

enum {
	MICROJXL__DIST_BITS = 12,
	MICROJXL__ANS_INIT_STATE = 0x130000
};

// the alias table of size N is conceptually an array of N buckets with probability 1/N each,
// where each bucket corresponds to at most two symbols distinguished by the cutoff point.
// this is done by rearranging symbols so that every symbol boundary falls into distinct buckets.
// so it allows *any* distribution of N symbols to be decoded in a constant time after the setup.
// the table is not unique though, so the spec needs to specify the exact construction algorithm.
//
//   input range: 0         cutoff               bucket_size
//                +-----------|----------------------------+
// output symbol: |     i     |           symbol           | <- bucket i
//                +-----------|----------------------------+
//  output range: 0     cutoff|offset    offset+bucket_size
typedef struct { int16_t cutoff, offset_or_next, symbol; } microjxl__alias_bucket;

MICROJXL__STATIC_RETURNS_ERR microjxl__init_alias_map(
	microjxl__st *st, const int16_t *D, int32_t log_alpha_size, microjxl__alias_bucket **out
);
MICROJXL_STATIC int32_t microjxl__ans_code(
	microjxl__st *st, uint32_t *state, int32_t log_bucket_size,
	const int16_t *D, const microjxl__alias_bucket *aliases
);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__init_alias_map(
	microjxl__st *st, const int16_t *D, int32_t log_alpha_size, microjxl__alias_bucket **out
) {
	int16_t log_bucket_size = (int16_t) (MICROJXL__DIST_BITS - log_alpha_size);
	int16_t bucket_size = (int16_t) (1 << log_bucket_size);
	int16_t table_size = (int16_t) (1 << log_alpha_size);
	microjxl__alias_bucket *buckets = NULL;
	// the underfull and overfull stacks are implicit linked lists; u/o resp. is the top index,
	// buckets[u/o].next is the second-to-top index and so on. an index -1 indicates the bottom.
	int16_t u = -1, o = -1, i, j;

	MICROJXL__ASSERT(5 <= log_alpha_size && log_alpha_size <= 8);
	MICROJXL__TRY_MALLOC(microjxl__alias_bucket, &buckets, 1u << log_alpha_size);

	for (i = 0; i < table_size && !D[i]; ++i);
	for (j = (int16_t) (i + 1); j < table_size && !D[j]; ++j);
	if (i < table_size && j >= table_size) { // D[i] is the only non-zero probability
		for (j = 0; j < table_size; ++j) {
			buckets[j].symbol = i;
			buckets[j].offset_or_next /*offset*/ = (int16_t) (j << log_bucket_size);
			buckets[j].cutoff = 0;
		}
		*out = buckets;
		return 0;
	}

	// each bucket is either settled (fields fully set) or unsettled (only `cutoff` is set).
	// unsettled buckets are either in the underfull stack, in which case `cutoff < bucket_size`,
	// or in the overfull stack, in which case `cutoff > bucket_size`. other fields are left
	// unused, so `offset` in settled buckets is aliased to `next` in unsettled buckets.
	// when rearranging results in buckets with `cutoff == bucket_size`,
	// final fields are set and they become settled; eventually every bucket has to be settled.
	for (i = 0; i < table_size; ++i) {
		int16_t cutoff = D[i];
		buckets[i].cutoff = cutoff;
		if (cutoff > bucket_size) {
			buckets[i].offset_or_next /*next*/ = o;
			o = i;
		} else if (cutoff < bucket_size) {
			buckets[i].offset_or_next /*next*/ = u;
			u = i;
		} else { // immediately settled
			buckets[i].symbol = i;
			buckets[i].offset_or_next /*offset*/ = 0;
		}
	}

	while (o >= 0) {
		int16_t by, tmp;
		MICROJXL__ASSERT(u >= 0);
		by = (int16_t) (bucket_size - buckets[u].cutoff);
		// move the input range [cutoff[o] - by, cutoff[o]] of the bucket o into
		// the input range [cutoff[u], bucket_size] of the bucket u (which is settled after this)
		tmp = buckets[u].offset_or_next /*next*/;
		buckets[o].cutoff = (int16_t) (buckets[o].cutoff - by);
		buckets[u].symbol = o;
		buckets[u].offset_or_next /*offset*/ = (int16_t) (buckets[o].cutoff - buckets[u].cutoff);
		u = tmp;
		if (buckets[o].cutoff < bucket_size) { // o is now underfull, move to the underfull stack
			tmp = buckets[o].offset_or_next /*next*/;
			buckets[o].offset_or_next /*next*/ = u;
			u = o;
			o = tmp;
		} else if (buckets[o].cutoff == bucket_size) { // o is also settled
			tmp = buckets[o].offset_or_next /*next*/;
			buckets[o].offset_or_next /*offset*/ = 0;
			o = tmp;
		}
	}

	MICROJXL__ASSERT(u < 0);
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_ALIAS")) { for (i = 0; i < table_size; ++i) fprintf(stderr, "[microjxl-alias] b=%d cutoff=%d sym=%d off=%d D=%d\n", i, buckets[i].cutoff, buckets[i].symbol, buckets[i].offset_or_next, D[i]); }
#endif
	*out = buckets;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(buckets);
	return st->err;
}

MICROJXL_STATIC int32_t microjxl__ans_code(
	microjxl__st *st, uint32_t *state, int32_t log_bucket_size,
	const int16_t *D, const microjxl__alias_bucket *aliases
) {
	if (*state == 0) {
		*state = (uint32_t) microjxl__u(st, 16);
		*state |= (uint32_t) microjxl__u(st, 16) << 16;
	}
	{
		int32_t index = (int32_t) (*state & 0xfff);
		int32_t i = index >> log_bucket_size;
		int32_t pos = index & ((1 << log_bucket_size) - 1);
		const microjxl__alias_bucket *bucket = &aliases[i];
		int32_t symbol = pos < bucket->cutoff ? i : bucket->symbol;
		int32_t offset = pos < bucket->cutoff ? 0 : bucket->offset_or_next /*offset*/;
		MICROJXL__ASSERT(D[symbol] != 0);
		*state = (uint32_t) D[symbol] * (*state >> 12) + (uint32_t) offset + (uint32_t) pos;
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_ANS")) fprintf(stderr, "[microjxl-ans] sym=%d state=%u\n", symbol, (unsigned) *state);
		if (getenv("MICROJXL_TRACE_BUCKET")) fprintf(stderr, "[microjxl-bucket] la=%d i=%d pos=%d cutoff=%d sym=%d off=%d freq=%d\n", 12 - log_bucket_size, i, pos, bucket->cutoff, symbol, offset, D[symbol]);
#endif
		if (*state < (1u << 16)) *state = (*state << 16) | (uint32_t) microjxl__u(st, 16);
		return symbol;
	}
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// entropy code

typedef union {
	microjxl__hybrid_int_config config;
	struct {
		microjxl__hybrid_int_config config;
		int32_t count;
	} init; // only used during the initialization
	struct {
		microjxl__hybrid_int_config config;
		int16_t *D;
		microjxl__alias_bucket *aliases;
	} ans; // if parent use_prefix_code is false
	struct {
		microjxl__hybrid_int_config config;
		int16_t fast_len, max_len;
		int32_t *table;
	} prefix; // if parent use_prefix_code is true
} microjxl__code_cluster;

typedef struct {
	int32_t num_dist;
	int lz77_enabled, use_prefix_code;
	int32_t min_symbol, min_length;
	int32_t log_alpha_size; // only used when use_prefix_code is false
	int32_t num_clusters; // in [1, min(num_dist, 256)]
	uint8_t *cluster_map; // each in [0, num_clusters)
	microjxl__hybrid_int_config lz_len_config;
	microjxl__code_cluster *clusters;
} microjxl__code_spec;

typedef struct {
	const microjxl__code_spec *spec;
	// LZ77 states
	int32_t num_to_copy, copy_pos, num_decoded;
	int32_t window_cap, *window;
	// ANS state (SPEC there is a single such state throughout the whole ANS stream)
	uint32_t ans_state; // 0 if uninitialized
} microjxl__code_st;

// microjxl__code dist_mult should be clamped to this value in order to prevent overflow
#define MICROJXL__MAX_DIST_MULT (1 << 21)

/* libjxl dec_frame.cc pads the AC context map by
 * kZeroDensityContextLimit - kZeroDensityContextCount = 16 zero entries
 * ("cheat in hot loop", dec_frame.cc:461): ZeroDensityContext can reach
 * 473 > 458 on corrupt streams. The AC coefficient codespec's cluster map
 * gets the same padding (entries map to cluster 0). */
#define MICROJXL__COEFF_CTX_PAD 16
MICROJXL__STATIC_RETURNS_ERR microjxl__pad_cluster_map(microjxl__st *st, microjxl__code_spec *spec, int32_t pad);
MICROJXL__STATIC_RETURNS_ERR microjxl__cluster_map(
	microjxl__st *st, int32_t num_dist, int32_t max_allowed, int32_t *num_clusters, uint8_t **outmap
);
MICROJXL__STATIC_RETURNS_ERR microjxl__ans_table(microjxl__st *st, int32_t log_alpha_size, int16_t **outtable);
MICROJXL__STATIC_RETURNS_ERR microjxl__read_code_spec(microjxl__st *st, int32_t num_dist, microjxl__code_spec *spec);
MICROJXL_STATIC void microjxl__init_code(microjxl__code_st *code, const microjxl__code_spec *spec);
MICROJXL_STATIC int32_t microjxl__entropy_code_cluster(
	microjxl__st *st, int use_prefix_code, int32_t log_alpha_size,
	microjxl__code_cluster *cluster, uint32_t *ans_state
);
MICROJXL_STATIC int32_t microjxl__code(microjxl__st *st, int32_t ctx, int32_t dist_mult, microjxl__code_st *code);
MICROJXL_STATIC void microjxl__mem_free_code(microjxl__code_st *code);
MICROJXL__STATIC_RETURNS_ERR microjxl__finish_and_free_code(microjxl__st *st, microjxl__code_st *code);
MICROJXL_STATIC void microjxl__mem_free_code_spec(microjxl__code_spec *spec);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__cluster_map(
	microjxl__st *st, int32_t num_dist, int32_t max_allowed, int32_t *num_clusters, uint8_t **outmap
) {
	microjxl__code_spec codespec = MICROJXL__INIT; // cluster map might be recursively coded
	microjxl__code_st code = MICROJXL__INIT;
	uint32_t seen[8] = {0};
	uint8_t *map = NULL;
	int32_t i, j;

	MICROJXL__ASSERT(num_dist > 0);
	MICROJXL__ASSERT(max_allowed >= 1 && max_allowed <= 256);
	if (max_allowed > num_dist) max_allowed = num_dist;

	if (num_dist == 1) { // SPEC impossible in Brotli but possible (and unspecified) in JPEG XL
		*num_clusters = 1;
		MICROJXL__TRY_CALLOC(uint8_t, outmap, 1);
		return 0;
	}
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_CM")) fprintf(stderr, "[microjxl-cm] start num_dist=%d bitpos=%lld\n", num_dist, (long long) microjxl__bits_read(st));
#endif

	*outmap = NULL;
	MICROJXL__TRY_MALLOC(uint8_t, &map, (size_t) num_dist);

	if (microjxl__u(st, 1)) { // is_simple (# clusters < 8)
		int32_t nbits = microjxl__u(st, 2);
		for (i = 0; i < num_dist; ++i) {
			map[i] = (uint8_t) microjxl__u(st, nbits);
			MICROJXL__SHOULD((int32_t) map[i] < max_allowed, "clst");
		}
	} else {
		int use_mtf = microjxl__u(st, 1);
		#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_CMTRACE")) fprintf(stderr, "[cmmtf] use_mtf=%d num_dist=%d\n", use_mtf, num_dist);
		#endif

		// TODO while num_dist is limited to 1, there is still a possibility of unbounded recursion
		// when each code spec introduces its own LZ77 distribution; libjxl doesn't allow LZ77
		// when cluster map is reading only two entries, which is technically incorrect but
		// easier to adopt in the current structure of microjxl as well.
		MICROJXL__TRY(microjxl__read_code_spec(st, num_dist <= 2 ? -1 : 1, &codespec));
		microjxl__init_code(&code, &codespec);
		for (i = 0; i < num_dist; ++i) {
			int32_t index = microjxl__code(st, 0, 0, &code); // SPEC context (always 0) is missing
			#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_CMTRACE")) fprintf(stderr, "[cmrd] i=%d sym=%d ntc=%d ndec=%d\n", i, index, code.num_to_copy, code.num_decoded);
			#endif
			MICROJXL__SHOULD(index < max_allowed, "clst");
			map[i] = (uint8_t) index;
		}
		MICROJXL__TRY(microjxl__finish_and_free_code(st, &code));
		microjxl__mem_free_code_spec(&codespec);

		if (use_mtf) {
			uint8_t mtf[256], moved;
			for (i = 0; i < 256; ++i) mtf[i] = (uint8_t) i;
			for (i = 0; i < num_dist; ++i) {
				j = map[i];
				map[i] = moved = mtf[j];
				for (; j > 0; --j) mtf[j] = mtf[j - 1];
				mtf[0] = moved;
			}
		}
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_CM")) { uint32_t h = 2166136261u; for (i = 0; i < num_dist; ++i) { h ^= map[i]; h *= 16777619u; } fprintf(stderr, "[microjxl-cm] done num_dist=%d nclusters=%d hash=%08x bitpos=%lld\n", num_dist, *num_clusters, h, (long long) microjxl__bits_read(st)); }
#endif
	}

	// determine the implicit num_clusters: SPEC/libjxl use max(map)+1
	// (VerifyContextMap requires every cluster < num_clusters to be present,
	// so the first unset position of `seen` equals max(map)+1 anyway — except
	// when the map uses only high IDs and position 0 is unset, where the old
	// loop wrongly returned 0).
	int32_t max_id = -1;
	for (i = 0; i < num_dist; ++i) {
		MICROJXL__SHOULD(map[i] < max_allowed, "clst");
		if ((int32_t) map[i] > max_id) max_id = map[i];
	}
	*num_clusters = max_id + 1;
	MICROJXL__ASSERT(*num_clusters > 0);

	*outmap = map;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(map);
	microjxl__mem_free_code(&code);
	microjxl__mem_free_code_spec(&codespec);
	return st->err;
}

/* Extend a cluster map with `pad` entries mapping to cluster 0 (libjxl's
 * context_map.resize(num_contexts + 16); see MICROJXL__COEFF_CTX_PAD).
 * The spec's num_dist grows accordingly so the assert in microjxl__code
 * accepts contexts inside the padded range; num_clusters is unchanged
 * (the padding maps to cluster 0, which always exists). */
MICROJXL__STATIC_RETURNS_ERR microjxl__pad_cluster_map(microjxl__st *st, microjxl__code_spec *spec, int32_t pad) {
	uint8_t *map;
	int32_t i;
	if (spec->cluster_map == NULL || pad <= 0) return 0;
	MICROJXL__TRY_CALLOC(uint8_t, &map, (size_t) (spec->num_dist + pad));
	for (i = 0; i < spec->num_dist; ++i) map[i] = spec->cluster_map[i];
	/* tail is already 0 = cluster 0 (calloc) */
	microjxl__mem_free(spec->cluster_map);
	spec->cluster_map = map;
	spec->num_dist += pad;
	return 0;
MICROJXL__ON_ERROR:
	microjxl__mem_free(map);
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__ans_table(microjxl__st *st, int32_t log_alpha_size, int16_t **outtable) {
	enum { DISTBITS = MICROJXL__DIST_BITS, DISTSUM = 1 << DISTBITS };
	int32_t table_size = 1 << log_alpha_size;
	int32_t i;
	int16_t *D = NULL;		MICROJXL__TRY_MALLOC(int16_t, &D, (size_t) table_size);

#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[jd] ans_table start table_size=%d bitpos=%lld\n", table_size, (long long) microjxl__bits_read(st));
#endif
	switch (microjxl__u(st, 2)) { // two Bool() calls combined into u(2), so bits are swapped
	case 1: { // true -> false case: one entry
		int32_t v = microjxl__u8(st);
		memset(D, 0, sizeof(int16_t) * (size_t) table_size);
		MICROJXL__SHOULD(v < table_size, "ansd");
		D[v] = DISTSUM;
		break;
	}

	case 3: { // true -> true case: two entries
		int32_t v1 = microjxl__u8(st);
		int32_t v2 = microjxl__u8(st);
		MICROJXL__SHOULD(v1 != v2 && v1 < table_size && v2 < table_size, "ansd");
		memset(D, 0, sizeof(int16_t) * (size_t) table_size);
		D[v1] = (int16_t) microjxl__u(st, DISTBITS);
		D[v2] = (int16_t) (DISTSUM - D[v1]);
		break;
	}

	case 2: { // false -> true case: evenly distribute to first `alpha_size` entries
		int32_t alpha_size = microjxl__u8(st) + 1;
		int16_t d = (int16_t) (DISTSUM / alpha_size);
		int16_t bias_size = (int16_t) (DISTSUM % alpha_size);
		MICROJXL__SHOULD(alpha_size <= table_size, "ansd");
		for (i = 0; i < bias_size; ++i) D[i] = (int16_t) (d + 1);
		for (; i < alpha_size; ++i) D[i] = d;
		for (; i < table_size; ++i) D[i] = 0;
		break;
	}

	case 0: { // false -> false case: bit counts + RLE
		int32_t len, shift, alpha_size, omit_log, omit_pos, code, total, n;
		int32_t ncodes, codes[259]; // exponents if >= 0, negated repeat count if < 0

		len = microjxl__u(st, 1) ? microjxl__u(st, 1) ? microjxl__u(st, 1) ? 3 : 2 : 1 : 0;
		shift = microjxl__u(st, len) + (1 << len) - 1;
		MICROJXL__SHOULD(shift <= 13, "ansd");
		alpha_size = microjxl__u8(st) + 3;

		omit_log = -1; // there should be at least one non-RLE code
		for (i = ncodes = 0; i < alpha_size; ) {
			static const int32_t TABLE[] = { // reinterpretation of kLogCountLut
				0xa0003,     -16, 0x70003, 0x30004, 0x60003, 0x80003, 0x90003, 0x50004,
				0xa0003, 0x40004, 0x70003, 0x10004, 0x60003, 0x80003, 0x90003, 0x20004,
				0x00011, 0xb0022, 0xc0003, 0xd0043, // overflow for ...0001
			};
			code = microjxl__prefix_code(st, 4, 7, TABLE);
			if (code < 13) {
				++i;
				codes[ncodes++] = code;
				if (omit_log < code) omit_log = code;
			} else {
				i += code = microjxl__u8(st) + 4;
				codes[ncodes++] = -code;
			}
		}
		MICROJXL__SHOULD(i == alpha_size && omit_log >= 0, "ansd");

		omit_pos = -1;
		for (i = n = total = 0; i < ncodes && n < table_size; ++i) {
			code = codes[i];
			if (code < 0) { // repeat
				int16_t prev = n > 0 ? D[n - 1] : 0;
				MICROJXL__SHOULD(prev >= 0, "ansd"); // implicit D[n] followed by RLE
				code = microjxl__min32(-code, table_size - n);
				total += (int32_t) prev * code;
				while (code-- > 0) D[n++] = prev;
			} else if (code == omit_log) { // the first longest D[n] is "omitted" (implicit)
				omit_pos = n;
				omit_log = -1; // this branch runs at most once
				D[n++] = -1;
			} else if (code < 2) {
				total += code;
				D[n++] = (int16_t) code;
			} else {
				int32_t bitcount;
				--code;
				bitcount = microjxl__min32(microjxl__max32(0, shift - ((DISTBITS - code) >> 1)), code);
				code = (1 << code) + (microjxl__u(st, bitcount) << (code - bitcount));
				total += code;
				D[n++] = (int16_t) code;
			}
		}
		for (; n < table_size; ++n) D[n] = 0;
		MICROJXL__SHOULD(omit_pos >= 0, "ansd");
		MICROJXL__SHOULD(total <= DISTSUM, "ansd");
		D[omit_pos] = (int16_t) (DISTSUM - total);
		break;
	}

	default: MICROJXL__UNREACHABLE();
	}

#ifdef MICROJXL_DEBUG
	{ uint32_t h = 2166136261u; for (i = 0; i < table_size; ++i) { h ^= (uint8_t)(D[i] & 255); h *= 16777619u; h ^= (uint8_t)((D[i] >> 8) & 255); h *= 16777619u; } fprintf(stderr, "[jd] ans_table done hash=%08x\n", h); }
#endif
	*outtable = D;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(D);
	return st->err;
}

// num_dist can be negative (and its absolute value is used) when a further use of LZ77 is disallowed
MICROJXL__STATIC_RETURNS_ERR microjxl__read_code_spec(microjxl__st *st, int32_t num_dist, microjxl__code_spec *spec) {
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_SPEC")) fprintf(stderr, "[microjxl-spec] called with num_dist=%d from %p\n", num_dist, __builtin_return_address(0));
#endif
	int32_t i;
	int allow_lz77;

	MICROJXL__ASSERT(num_dist != 0);
	allow_lz77 = (num_dist > 0);
#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[microjxl] rcs entry: num_dist=%d\n", num_dist);
#endif
	num_dist = microjxl__abs32(num_dist);

	spec->cluster_map = NULL;
	spec->clusters = NULL;
	// LZ77Params
	spec->lz77_enabled = microjxl__u(st, 1);
	if (spec->lz77_enabled) {
		MICROJXL__SHOULD(allow_lz77, "lz77");
		spec->min_symbol = microjxl__u32(st, 224, 0, 512, 0, 4096, 0, 8, 15);
		spec->min_length = microjxl__u32(st, 3, 0, 4, 0, 5, 2, 9, 8);
		MICROJXL__TRY(microjxl__read_hybrid_int_config(st, 8, &spec->lz_len_config));
		++num_dist; // num_dist - 1 is a synthesized LZ77 length distribution
	} else {
		spec->min_symbol = spec->min_length = 0x7fffffff;
	}
	// cluster_map: a mapping from context IDs to actual distributions
	MICROJXL__TRY(microjxl__cluster_map(st, num_dist, 256, &spec->num_clusters, &spec->cluster_map));
#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[microjxl] spec: num_dist=%d num_clusters=%d bitpos=%lld\n", num_dist, spec->num_clusters, (long long) microjxl__bits_read(st));
#endif

	MICROJXL__TRY_CALLOC(microjxl__code_cluster, &spec->clusters, (size_t) spec->num_clusters);
	spec->use_prefix_code = microjxl__u(st, 1);
#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[microjxl] spec: use_prefix=%d\n", spec->use_prefix_code);
#endif
	if (spec->use_prefix_code) {
		for (i = 0; i < spec->num_clusters; ++i) { // SPEC the count is off by one
			MICROJXL__TRY(microjxl__read_hybrid_int_config(st, 15, &spec->clusters[i].config));
		}

		for (i = 0; i < spec->num_clusters; ++i) {
			if (microjxl__u(st, 1)) {
				int32_t n = microjxl__u(st, 4);
				spec->clusters[i].init.count = 1 + (1 << n) + microjxl__u(st, n);
				MICROJXL__SHOULD(spec->clusters[i].init.count <= (1 << 15), "hufd");
			} else {
				spec->clusters[i].init.count = 1;
			}
		}

		// SPEC this should happen after reading *all* count[i]
		for (i = 0; i < spec->num_clusters; ++i) {
			microjxl__code_cluster *c = &spec->clusters[i];
			int32_t fast_len, max_len;
			MICROJXL__TRY(microjxl__prefix_code_tree(st, c->init.count, &fast_len, &max_len, &c->prefix.table));
			c->prefix.fast_len = (int16_t) fast_len;
			c->prefix.max_len = (int16_t) max_len;
		}
	} else {
		spec->log_alpha_size = 5 + microjxl__u(st, 2);
#ifdef MICROJXL_DEBUG
		fprintf(stderr, "[microjxl] spec: log_alpha_size=%d\n", spec->log_alpha_size);
#endif
		for (i = 0; i < spec->num_clusters; ++i) { // SPEC the count is off by one
			MICROJXL__TRY(microjxl__read_hybrid_int_config(st, spec->log_alpha_size, &spec->clusters[i].config));
		}

		for (i = 0; i < spec->num_clusters; ++i) {
			microjxl__code_cluster *c = &spec->clusters[i];
			MICROJXL__TRY(microjxl__ans_table(st, spec->log_alpha_size, &c->ans.D));
			MICROJXL__TRY(microjxl__init_alias_map(st, c->ans.D, spec->log_alpha_size, &c->ans.aliases));
		}
	}

	spec->num_dist = num_dist;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_code_spec(spec);
	return st->err;
}

MICROJXL_STATIC void microjxl__init_code(microjxl__code_st *code, const microjxl__code_spec *spec) {
	code->spec = spec;
	code->num_to_copy = code->copy_pos = code->num_decoded = 0;
	code->window_cap = 0;
	code->window = 0;
	code->ans_state = 0;
}

MICROJXL_STATIC int32_t microjxl__entropy_code_cluster(
	microjxl__st *st, int use_prefix_code, int32_t log_alpha_size,
	microjxl__code_cluster *cluster, uint32_t *ans_state
) {
	if (use_prefix_code) {
		return microjxl__prefix_code(st, cluster->prefix.fast_len, cluster->prefix.max_len, cluster->prefix.table);
	} else {
		return microjxl__ans_code(st, ans_state, MICROJXL__DIST_BITS - log_alpha_size, cluster->ans.D, cluster->ans.aliases);
	}
}

// aka DecodeHybridVarLenUint
MICROJXL_STATIC int32_t microjxl__code(microjxl__st *st, int32_t ctx, int32_t dist_mult, microjxl__code_st *code) {
	static const int32_t MASK = 0xfffff;

	const microjxl__code_spec *spec = code->spec;
	int32_t token, distance, log_alpha_size;
	microjxl__code_cluster *cluster;
	int use_prefix_code;

#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_LZ2") && code->num_to_copy == 0 && code->num_decoded >= 22 && code->num_decoded <= 28) fprintf(stderr, "[microjxl-lz2] pre ndec=%d ntc=%d ctx=%d cluster=%d\n", code->num_decoded, code->num_to_copy, ctx, spec->cluster_map[ctx]);
#endif

	if (code->num_to_copy > 0) {
		MICROJXL__ASSERT(code->window); // because this can't be the initial token and lz77_enabled is true
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_MTRACE")) fprintf(stderr, "[mrd] ntc=%d ndec=%d COPY ret=%d\n", code->num_to_copy, code->num_decoded, code->window[code->copy_pos & MASK]);
#endif
		--code->num_to_copy;
		return code->window[code->num_decoded++ & MASK] = code->window[code->copy_pos++ & MASK];
	}		MICROJXL__ASSERT(ctx < spec->num_dist);
	use_prefix_code = spec->use_prefix_code;
	log_alpha_size = spec->log_alpha_size;
	cluster = &spec->clusters[spec->cluster_map[ctx]];
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_ANS") && code->spec->num_dist > 1) fprintf(stderr, "[microjxl-ans] ctx=%d cluster=%d\n", ctx, spec->cluster_map[ctx]);
#endif
	token = microjxl__entropy_code_cluster(st, use_prefix_code, log_alpha_size, cluster, &code->ans_state);
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_MTRACE")) fprintf(stderr, "[mrd] tok=%d ctx=%d ntc=%d ndec=%d\n", token, spec->cluster_map ? spec->cluster_map[ctx] : ctx, code->num_to_copy, code->num_decoded);
#endif
	if (token >= spec->min_symbol) { // this is large enough if lz77_enabled is false
		microjxl__code_cluster *lz_cluster = &spec->clusters[spec->cluster_map[spec->num_dist - 1]];
		int32_t num_to_copy = microjxl__hybrid_int(st, token - spec->min_symbol, spec->lz_len_config) + spec->min_length;
		token = microjxl__entropy_code_cluster(st, use_prefix_code, log_alpha_size, lz_cluster, &code->ans_state);
		distance = microjxl__hybrid_int(st, token, lz_cluster->config);
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_MTRACE")) fprintf(stderr, "[mrd] DTOK tok=%d dist=%d mult=%d\n", token, distance, dist_mult);
#endif
		if (st->err) return 0;
		if (!dist_mult) {
			++distance;
		} else if (distance >= 120) {
			distance -= 119;
		} else {
			static const uint8_t SPECIAL_DISTANCES[120] = { // {a,b} encoded as (a+7)*16+b
				0x71, 0x80, 0x81, 0x61, 0x72, 0x90, 0x82, 0x62, 0x91, 0x51, 0x92, 0x52,
				0x73, 0xa0, 0x83, 0x63, 0xa1, 0x41, 0x93, 0x53, 0xa2, 0x42, 0x74, 0xb0,
				0x84, 0x64, 0xb1, 0x31, 0xa3, 0x43, 0x94, 0x54, 0xb2, 0x32, 0x75, 0xa4,
				0x44, 0xb3, 0x33, 0xc0, 0x85, 0x65, 0xc1, 0x21, 0x95, 0x55, 0xc2, 0x22,
				0xb4, 0x34, 0xa5, 0x45, 0xc3, 0x23, 0x76, 0xd0, 0x86, 0x66, 0xd1, 0x11,
				0x96, 0x56, 0xd2, 0x12, 0xb5, 0x35, 0xc4, 0x24, 0xa6, 0x46, 0xd3, 0x13,
				0x77, 0xe0, 0x87, 0x67, 0xc5, 0x25, 0xe1, 0x01, 0xb6, 0x36, 0xd4, 0x14,
				0x97, 0x57, 0xe2, 0x02, 0xa7, 0x47, 0xe3, 0x03, 0xc6, 0x26, 0xd5, 0x15,
				0xf0, 0xb7, 0x37, 0xe4, 0x04, 0xf1, 0xf2, 0xd6, 0x16, 0xf3, 0xc7, 0x27,
				0xe5, 0x05, 0xf4, 0xd7, 0x17, 0xe6, 0x06, 0xf5, 0xe7, 0x07, 0xf6, 0xf7,
			};
			int32_t special = (int32_t) SPECIAL_DISTANCES[distance];
			MICROJXL__ASSERT(dist_mult <= MICROJXL__MAX_DIST_MULT);
			// TODO spec bug: distance can be as low as -6 when dist_mult = 1 and distance =
			// dist_mult * 1 - 7; libjxl clamps it to the minimum of 1, so we do the same here
			distance = microjxl__max32(1, ((special >> 4) - 7) + dist_mult * (special & 7));
		}
		distance = microjxl__min32(microjxl__min32(distance, code->num_decoded), 1 << 20);
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_MTRACE")) fprintf(stderr, "[mrd] DIST dist=%d ndec=%d\n", distance, code->num_decoded);
#endif
		code->copy_pos = code->num_decoded - distance;
		if (MICROJXL_UNLIKELY(distance == 0)) {
			// TODO spec bug: this is possible when num_decoded == 0 (or a non-positive special
			// distance, handled above) and libjxl acts as if `window[i]` is initially filled with 0
			MICROJXL__ASSERT(code->num_decoded == 0 && !code->window);
			code->window = (int32_t*) microjxl__calloc(1u << 20, sizeof(int32_t));
			if (!code->window) return MICROJXL__ERR("!mem"), 0;
		}
		MICROJXL__ASSERT(num_to_copy > 0);
		code->num_to_copy = num_to_copy - 1;
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_MTRACE")) fprintf(stderr, "[mrd] ntc=%d ndec=%d INITCOPY ret=%d\n", code->num_to_copy, code->num_decoded, code->window[code->copy_pos & MASK]);
#endif
		return code->window[code->num_decoded++ & MASK] = code->window[code->copy_pos++ & MASK];
	}

	token = microjxl__hybrid_int(st, token, cluster->config);
	if (st->err) return 0;
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_MTRACE")) fprintf(stderr, "[mrd] ctx=%d ret=%d ntc=%d ndec=%d\n", spec->cluster_map ? spec->cluster_map[ctx] : ctx, token, code->num_to_copy, code->num_decoded);
#endif
	if (spec->lz77_enabled) {
		if (!code->window) { // XXX should be dynamically resized
			code->window = (int32_t*) microjxl__malloc(1u << 20, sizeof(int32_t));
			if (!code->window) return MICROJXL__ERR("!mem"), 0;
		}
		code->window[code->num_decoded++ & MASK] = token;
	}
	return token;
}

MICROJXL_STATIC void microjxl__mem_free_code(microjxl__code_st *code) {
	microjxl__mem_free(code->window);
	code->window = NULL;
	code->window_cap = 0;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__finish_and_free_code(microjxl__st *st, microjxl__code_st *code) {
#ifdef MICROJXL_DEBUG
	static int microjxl_fin_site = 0;
	if (getenv("MICROJXL_TRACE_FIN")) fprintf(stderr, "[microjxl-fin] site=%d line=%d\n", ++microjxl_fin_site, __LINE__);
#endif
	if (!code->spec->use_prefix_code) {
		if (code->ans_state) {
#ifdef MICROJXL_DEBUG
			fprintf(stderr, "[microjxl] ans finish: state=%u init=%u ndec=%d ndist=%d\n", (unsigned) code->ans_state, (unsigned) MICROJXL__ANS_INIT_STATE, code->num_decoded, code->spec->num_dist);
#endif
			MICROJXL__SHOULD(code->ans_state == MICROJXL__ANS_INIT_STATE, "ans?");
		} else { // edge case: if no symbols have been read the state has to be read at this point
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_FIN")) fprintf(stderr, "[microjxl-fin] no-symbols branch: bitpos=%lld\n", (long long) microjxl__bits_read(st));
#endif
			MICROJXL__SHOULD(microjxl__u(st, 16) == (MICROJXL__ANS_INIT_STATE & 0xffff), "ans?");
			MICROJXL__SHOULD(microjxl__u(st, 16) == (MICROJXL__ANS_INIT_STATE >> 16), "ans?");
		}
	}
	// it's explicitly allowed that num_to_copy can be > 0 at the end of stream
MICROJXL__ON_ERROR:
	microjxl__mem_free_code(code);
	return st->err;
}

MICROJXL_STATIC void microjxl__mem_free_code_spec(microjxl__code_spec *spec) {
	int32_t i;
	if (spec->clusters) {
		for (i = 0; i < spec->num_clusters; ++i) {
			if (spec->use_prefix_code) {
				microjxl__mem_free(spec->clusters[i].prefix.table);
			} else {
				microjxl__mem_free(spec->clusters[i].ans.D);
				microjxl__mem_free(spec->clusters[i].ans.aliases);
			}
		}
		microjxl__mem_free(spec->clusters);
		spec->clusters = NULL;
	}
	microjxl__mem_free(spec->cluster_map);
	spec->cluster_map = NULL;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// image header & metadata

enum {
	MICROJXL__CHROMA_WHITE = 0, MICROJXL__CHROMA_RED = 1,
	MICROJXL__CHROMA_GREEN = 2, MICROJXL__CHROMA_BLUE = 3,
};

enum microjxl__ec_type {
	MICROJXL__EC_ALPHA = 0, MICROJXL__EC_DEPTH = 1, MICROJXL__EC_SPOT_COLOUR = 2,
	MICROJXL__EC_SELECTION_MASK = 3, MICROJXL__EC_BLACK = 4, MICROJXL__EC_CFA = 5,
	MICROJXL__EC_THERMAL = 6, MICROJXL__EC_NON_OPTIONAL = 15, MICROJXL__EC_OPTIONAL = 16,
};

typedef struct {
	enum microjxl__ec_type type;
	int32_t bpp, exp_bits, dim_shift, name_len;
	char *name;
	union {
		int alpha_associated;
		struct { float red, green, blue, solidity; } spot;
		int32_t cfa_channel;
	} data;
} microjxl__ec_info;

enum microjxl__orientation {
	MICROJXL__ORIENT_TL = 1, MICROJXL__ORIENT_TR = 2, MICROJXL__ORIENT_BR = 3, MICROJXL__ORIENT_BL = 4,
	MICROJXL__ORIENT_LT = 5, MICROJXL__ORIENT_RT = 6, MICROJXL__ORIENT_RB = 7, MICROJXL__ORIENT_LB = 8,
};

enum microjxl__cspace {
	MICROJXL__CS_CHROMA = 'c', MICROJXL__CS_GREY = 'g', MICROJXL__CS_XYB = 'x',
};

enum { // for `microjxl__image_st.gamma_or_tf`
	MICROJXL__TF_709 = -1, MICROJXL__TF_UNKNOWN = -2, MICROJXL__TF_LINEAR = -8, MICROJXL__TF_SRGB = -13,
	MICROJXL__TF_PQ = -16, MICROJXL__TF_DCI = -17, MICROJXL__TF_HLG = -18,
	MICROJXL__GAMMA_MAX = 10000000,
};

enum microjxl__render_intent {
	MICROJXL__INTENT_PERC = 0, MICROJXL__INTENT_REL = 1, MICROJXL__INTENT_SAT = 2, MICROJXL__INTENT_ABS = 3
};

typedef struct microjxl__image_st {
	int32_t width, height;
	enum microjxl__orientation orientation;
	int32_t intr_width, intr_height; // 0 if not specified
	int32_t preview_w, preview_h; // 0 if no preview
	int bpp, exp_bits;

	int32_t anim_tps_num, anim_tps_denom; // num=denom=0 if not animated
	int64_t anim_nloops; // 0 if infinity
	int anim_have_timecodes;

	char *icc;
	size_t iccsize;
	/* HLG output OOTF state (lazy): Y row of opsin_inv_mat folded with the
	 * primaries' luminances, and the ToSceneLight exponent */
	float hlg_yeff[3], hlg_gamma;
	int hlg_init;
	enum microjxl__cspace cspace;
	float cpoints[4 /*MICROJXL__CHROMA_xxx*/][2 /*x=0, y=1*/]; // only for MICROJXL__CS_CHROMA
	int32_t gamma_or_tf; // gamma if > 0, transfer function if <= 0
	enum microjxl__render_intent render_intent;
	float intensity_target, min_nits; // 0 < min_nits <= intensity_target
	float linear_below; // absolute (nits) if >= 0; a negated ratio of max display brightness if [-1,0]

	int modular_16bit_buffers;
	int num_extra_channels;
	microjxl__ec_info *ec_info;
	int xyb_encoded;
	/* Spot-colour rendering toggle (libjxl --norender_spotcolors): when 0,
	 * spot ECs are left as raw extra-channel samples and never composited
	 * onto the colour output (conformance / image-editing workflows). */
	int render_spot;
	float relative_to_max_display;
	float opsin_inv_mat[3][3], opsin_bias[3], quant_bias[3 /*xyb*/], quant_bias_num;
	int want_icc;
	/* custom upsampling weights (D.3): cw_mask bit 0/1/2 signals 15/55/210
	 * f16 weights for the 2x/4x/8x kernels; unset bits use the defaults */
	uint32_t up_cw_mask;
	float up2_weight[15], up4_weight[55], up8_weight[210];

	/* Saved reference frames (K.3), indexed by save_as_reference (0..3).
	 * Planes are full-frame floats in the same domain as the render
	 * pipeline's pre-colour-transform rows: XYB (recorrelated X/Y/B) for
	 * xyb_encoded frames, RGB/gamma-domain otherwise; extra channels use
	 * the same scaling as the render's integer planes. ref_in_xyb mirrors
	 * libjxl's ib_is_in_xyb (save_before_color_transform). Patches can
	 * only use in-xyb references. */
	microjxl__plane *ref_planes[4]; // [ref][3 + num_ec] or NULL
	int32_t ref_nch[4], ref_w[4], ref_h[4];
	int ref_in_xyb[4], ref_present[4];
	/* K.5.2/LF frames (libjxl "DC frames", spec LFFrame[0..3]): the
	 * decoded LF of a future frame, one float XYB plane triplet per
	 * level (dc_level 1..4). Stored at the DC frame's own resolution,
	 * which is DivCeil(image size, 8^dc_level). The planes hold the
	 * CfL-corrected dequantized LF samples (recorrelated X/Y/B floats,
	 * exactly the domain of lf_quant's output). */
	microjxl__plane lf_frames[4][3]; // indexed [dc_level - 1][xyb]
	int lf_frame_present[4];
	/* K.5.2 noise frame counters across the whole stream */
	int64_t vis_frame_idx, nonvis_frame_idx;
	/* K.5.2 animation canvas: the accumulated (blended) display image,
	 * RGBA floats [0,1] at full image resolution; NULL until a frame
	 * requiring blending is displayed. Freed with the rest of the image
	 * state. */
	microjxl__plane *canvas; // owned; NULL when no blending has happened
	/* Display-domain RGBA render of the most recent *referencable* frame
	 * (libjxl saves references from the post-blending pipeline), used as
	 * the background for canvas compositing. NULL until the first such
	 * frame; zeroes substitute for a missing slot (libjxl's zeroes_). */
	microjxl__plane *ref_render; // owned; NULL when no background yet
	/* Display-domain RGBA render of the post-blend canvas, per reference
	 * slot (libjxl saves references from the post-blending pipeline: the
	 * image bundle written by WriteToImageBundleStage / stored by
	 * FinalizeFrame is what later blends use as their background). Indexed
	 * by save_as_ref of the visible referencable frame; an absent slot
	 * blends over zeroes (libjxl's zeroes_). Patch-domain reference data
	 * lives in ref_planes above; ref_rgba holds the coalesced DISPLAY
	 * render (R, G, B, blend-alpha planes, [0,1] floats). */
	microjxl__plane ref_rgba[4]; // value planes; type==0 (fresh/calloc) = absent slot
	/* Coalesced (canvas-composited) extra-channel planes (libjxl's
	 * frame_storage_for_referencing / dec_state full_image EC planes):
	 * num_ec_canvas entries, each canvas-sized (im->width x im->height),
	 * sample domain 0..maxval (I16/I32 raw values, matching the frame's
	 * own EC planes — a VarDCT frame's float alpha carrier lands here as
	 * an F32 canvas, read as already-normalized). NULL until the first frame carrying ECs is
	 * composited; the array must cover every EC in the image. */
	microjxl__plane *ec_canvas; // owned; array of planes or NULL
	int32_t num_ec_canvas;
	/* Per-slot snapshot of the EC canvas at store time (libjxl's
	 * reference_frames[slot].frame->extra_channels(): FinalizeFrame saves
	 * the full image bundle, ECs included, and stage_blending reads each
	 * EC's background from its OWN blend source slot — including the
	 * padding rows/columns outside the frame rect, which ProcessPaddingRow
	 * fills from the same slot, or zeroes when the slot is absent). */
	microjxl__plane *ref_ec[4]; // [slot][ec]; NULL entries = absent
	int32_t ref_ec_n[4];
} microjxl__image_st;

MICROJXL__STATIC_RETURNS_ERR microjxl__signature(microjxl__st *st);
MICROJXL__STATIC_RETURNS_ERR microjxl__size_header(microjxl__st *st, int32_t *outw, int32_t *outh);
MICROJXL__STATIC_RETURNS_ERR microjxl__bit_depth(microjxl__st *st, int32_t *outbpp, int32_t *outexpbits);
MICROJXL__STATIC_RETURNS_ERR microjxl__name(microjxl__st *st, int32_t *outlen, char **outbuf);
MICROJXL__STATIC_RETURNS_ERR microjxl__customxy(microjxl__st *st, float xy[2]);
MICROJXL__STATIC_RETURNS_ERR microjxl__extensions(microjxl__st *st);
MICROJXL__STATIC_RETURNS_ERR microjxl__image_metadata(microjxl__st *st);
MICROJXL_STATIC void microjxl__mem_free_image_state(microjxl__image_st *im);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__signature(microjxl__st *st) {
	int32_t sig = microjxl__u(st, 16);
	MICROJXL__SHOULD(sig == 0x0aff, "!jxl"); // FF 0A in the byte sequence
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__size_header(microjxl__st *st, int32_t *outw, int32_t *outh) {
	int32_t div8 = microjxl__u(st, 1);
	*outh = div8 ? (microjxl__u(st, 5) + 1) * 8 : microjxl__u32(st, 1, 9, 1, 13, 1, 18, 1, 30);
	switch (microjxl__u(st, 3)) { // ratio
	case 0: *outw = div8 ? (microjxl__u(st, 5) + 1) * 8 : microjxl__u32(st, 1, 9, 1, 13, 1, 18, 1, 30); break;
	case 1: *outw = *outh; break;
	case 2: *outw = (int32_t) ((uint64_t) *outh * 6 / 5); break;
	case 3: *outw = (int32_t) ((uint64_t) *outh * 4 / 3); break;
	case 4: *outw = (int32_t) ((uint64_t) *outh * 3 / 2); break;
	case 5: *outw = (int32_t) ((uint64_t) *outh * 16 / 9); break;
	case 6: *outw = (int32_t) ((uint64_t) *outh * 5 / 4); break;
	case 7:
		// height is at most 2^30, so width is at most 2^31 which requires uint32_t.
		// but in order to avoid bugs we rarely use unsigned integers, so we just reject it.
		// this should be not a problem as the Main profile Level 10 (the largest profile)
		// already limits height to at most 2^30.
		MICROJXL__SHOULD(*outh < 0x40000000, "bigg");
		*outw = *outh * 2;
		break;
	default: MICROJXL__UNREACHABLE();
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__bit_depth(microjxl__st *st, int32_t *outbpp, int32_t *outexpbits) {
	if (microjxl__u(st, 1)) { // float_sample
		int32_t mantissa_bits;
		*outbpp = microjxl__u32(st, 32, 0, 16, 0, 24, 0, 1, 6);
		*outexpbits = microjxl__u(st, 4) + 1;
		mantissa_bits = *outbpp - *outexpbits - 1;
		MICROJXL__SHOULD(2 <= mantissa_bits && mantissa_bits <= 23, "bpp?");
		MICROJXL__SHOULD(2 <= *outexpbits && *outexpbits <= 8, "exp?"); // implies bpp in [5,32] when combined
	} else {
		*outbpp = microjxl__u32(st, 8, 0, 10, 0, 12, 0, 1, 6);
		*outexpbits = 0;
		MICROJXL__SHOULD(1 <= *outbpp && *outbpp <= 31, "bpp?");
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__name(microjxl__st *st, int32_t *outlen, char **outbuf) {
	char *buf = NULL;
	int32_t i, c, cc, len;
	len = microjxl__u32(st, 0, 0, 0, 4, 16, 5, 48, 10);
	if (len > 0) {
		MICROJXL__TRY_MALLOC(char, &buf, (size_t) len + 1);
		for (i = 0; i < len; ++i) {
			buf[i] = (char) microjxl__u(st, 8);
			MICROJXL__RAISE_DELAYED();
		}
		buf[len] = 0;
		for (i = 0; i < len; ) { // UTF-8 verification
			c = (uint8_t) buf[i++];
			cc = (uint8_t) buf[i]; // always accessible thanks to null-termination
			c = c < 0x80 ? 0 : c < 0xc2 ? -1 : c < 0xe0 ? 1 :
				c < 0xf0 ? (c == 0xe0 ? cc >= 0xa0 : c == 0xed ? cc < 0xa0 : 1) ? 2 : -1 :
				c < 0xf5 ? (c == 0xf0 ? cc >= 0x90 : c == 0xf4 ? cc < 0x90 : 1) ? 3 : -1 : -1;
			MICROJXL__SHOULD(c >= 0 && i + c <= len, "name"); // [i, i+c) must fit; == len is valid (end of string)
			while (c-- > 0) MICROJXL__SHOULD((buf[i++] & 0xc0) == 0x80, "name");
		}
		*outbuf = buf;
	} else {
		MICROJXL__RAISE_DELAYED();
		*outbuf = NULL;
	}
	*outlen = len;
	return 0;
MICROJXL__ON_ERROR:
	microjxl__mem_free(buf);
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__customxy(microjxl__st *st, float xy[2]) {
	xy[0] = (float)microjxl__unpack_signed(microjxl__u32(st, 0, 19, 0x80000, 19, 0x100000, 20, 0x200000, 21)) / 100000.0f;
	xy[1] = (float)microjxl__unpack_signed(microjxl__u32(st, 0, 19, 0x80000, 19, 0x100000, 20, 0x200000, 21)) / 100000.0f;
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__extensions(microjxl__st *st) {
	uint64_t extensions = microjxl__u64(st);
	int64_t nbits = 0;
	int32_t i;
	for (i = 0; i < 64; ++i) {
		if (extensions >> i & 1) {
			uint64_t n = microjxl__u64(st);
			MICROJXL__RAISE_DELAYED();
			MICROJXL__SHOULD(n <= (uint64_t) INT64_MAX && microjxl__add64(nbits, (int64_t) n, &nbits), "flen");
		}
	}
	return microjxl__skip(st, nbits);
MICROJXL__ON_ERROR:
	return st->err;
}

/* 3x3 row-major helpers (libjxl base/matrix_ops.h Mul3x3Matrix/Inv3x3Matrix) */
static void microjxl__mul3x3(const float a[3][3], const float b[3][3], float c[3][3]) {
	float t[3][3];
	int i, j, k;
	for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) {
		t[i][j] = 0.0f;
		for (k = 0; k < 3; ++k) t[i][j] += a[i][k] * b[k][j];
	}
	memcpy(c, t, sizeof t);
}

static int microjxl__inv3x3(float m[3][3]) { /* in-place; 0 on singular */
	float det, c[3][3];
	int i, j;
	det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
		- m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
		+ m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	if (det == 0.0f || !isfinite(det)) return 0;
	c[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
	c[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
	c[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
	c[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
	c[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
	c[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
	c[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
	c[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
	c[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
	memcpy(m, c, sizeof c);
	for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) if (!isfinite(m[i][j])) return 0;
	return 1;
}

/* PrimariesToXYZ (jxl_cms_internal.h): RGB->XYZ from chromaticities;
 * cp = {white, red, green, blue} xy pairs. 0 on failure. */
static int microjxl__primaries_to_xyz(const float cp[4][2], float m[3][3]) {
	float p[3][3], pinv[3][3], w[3], xyz[3];
	int i, j;
	if (!(cp[0][0] >= 0 && cp[0][0] <= 1 && cp[0][1] > 0 && cp[0][1] <= 1)) return 0;
	p[0][0] = cp[1][0]; p[0][1] = cp[2][0]; p[0][2] = cp[3][0];
	p[1][0] = cp[1][1]; p[1][1] = cp[2][1]; p[1][2] = cp[3][1];
	p[2][0] = 1 - cp[1][0] - cp[1][1];
	p[2][1] = 1 - cp[2][0] - cp[2][1];
	p[2][2] = 1 - cp[3][0] - cp[3][1];
	memcpy(pinv, p, sizeof p);
	if (!microjxl__inv3x3(pinv)) return 0;
	w[0] = cp[0][0] / cp[0][1]; w[1] = 1.0f; w[2] = (1 - cp[0][0] - cp[0][1]) / cp[0][1];
	if (!isfinite(w[0]) || !isfinite(w[2])) return 0;
	for (i = 0; i < 3; ++i)
		xyz[i] = pinv[i][0] * w[0] + pinv[i][1] * w[1] + pinv[i][2] * w[2];
	for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) m[i][j] = p[i][j] * xyz[j];
	return 1;
}

/* AdaptToXYZD50 (jxl_cms_internal.h): Bradford chromatic adaptation from
 * white point (wx, wy) to D50. 0 on failure. */
static int microjxl__adapt_to_xyzd50(float wx, float wy, float m[3][3]) {
	static const float BRADFORD[3][3] = {
		{0.8951f, 0.2664f, -0.1614f},
		{-0.7502f, 1.7135f, 0.0367f},
		{0.0389f, -0.0685f, 1.0296f},
	};
	static const float BRADFORD_INV[3][3] = {
		{0.9869929f, -0.1470543f, 0.1599627f},
		{0.4323053f, 0.5183603f, 0.0492912f},
		{-0.0085287f, 0.0400428f, 0.9684867f},
	};
	static const float D50[3] = {0.96422f, 1.0f, 0.82521f};
	float w[3], lms[3], lms50[3], a[3][3], b[3][3];
	int i;
	if (!(wx >= 0 && wx <= 1 && wy > 0 && wy <= 1)) return 0;
	w[0] = wx / wy; w[1] = 1.0f; w[2] = (1 - wx - wy) / wy;
	if (!isfinite(w[0]) || !isfinite(w[2])) return 0;
	for (i = 0; i < 3; ++i) {
		lms[i] = BRADFORD[i][0] * w[0] + BRADFORD[i][1] * w[1] + BRADFORD[i][2] * w[2];
		lms50[i] = BRADFORD[i][0] * D50[0] + BRADFORD[i][1] * D50[1] + BRADFORD[i][2] * D50[2];
		if (lms[i] == 0.0f) return 0;
	}
	for (i = 0; i < 3; ++i) {
		a[i][0] = a[i][1] = a[i][2] = 0.0f;
		a[i][i] = lms50[i] / lms[i];
		if (!isfinite(a[i][i])) return 0;
	}
	microjxl__mul3x3(a, BRADFORD, b);
	microjxl__mul3x3(BRADFORD_INV, b, m);
	return 1;
}

/* XYB output for non-sRGB primaries (libjxl dec_xyb.cc SetColorEncoding):
 * the opsin inverse matrix is folded with srgb_to_original so the XYB->RGB
 * conversion directly yields linear RGB in the image's own primaries:
 *   srgb_to_xyzd50     = AdaptToXYZD50(srgb white) * PrimariesToXYZ(srgb)
 *   xyzd50_to_original = inv(AdaptToXYZD50(orig white) * PrimariesToXYZ(orig))
 *   srgb_to_original   = xyzd50_to_original * srgb_to_xyzd50
 *   inverse           *= srgb_to_original
 * Without the fold every XYB image with non-sRGB primaries (Rec.2100, P3, ...)
 * renders with wrong chromaticities (red-dominant bias). The fold also makes
 * microjxl__hlg_ootf_init's luminance weights correct: they fold Y through
 * opsin_inv_mat, which now maps into the original primaries. */
static void microjxl__fold_primaries_into_opsin_inv(microjxl__image_st *im) {
	static const float SRGB_CP[4][2] = {
		{0.3127f, 0.3290f}, {0.639998686f, 0.330010138f},
		{0.300003784f, 0.600003357f}, {0.150002046f, 0.059997204f},
	};
	float srgb_to_xyz[3][3], srgb_ad[3][3], orig_to_xyz[3][3], orig_ad[3][3];
	float srgb_to_xyzd50[3][3], orig_to_xyzd50[3][3], srgb_to_orig[3][3];
	if (!microjxl__primaries_to_xyz(SRGB_CP, srgb_to_xyz)) return;
	if (!microjxl__primaries_to_xyz(im->cpoints, orig_to_xyz)) return;
	if (!microjxl__adapt_to_xyzd50(SRGB_CP[0][0], SRGB_CP[0][1], srgb_ad)) return;
	if (!microjxl__adapt_to_xyzd50(im->cpoints[0][0], im->cpoints[0][1], orig_ad)) return;
	microjxl__mul3x3(srgb_ad, srgb_to_xyz, srgb_to_xyzd50);
	microjxl__mul3x3(orig_ad, orig_to_xyz, orig_to_xyzd50);
	if (!microjxl__inv3x3(orig_to_xyzd50)) return;
	microjxl__mul3x3(orig_to_xyzd50, srgb_to_xyzd50, srgb_to_orig);
	microjxl__mul3x3(srgb_to_orig, im->opsin_inv_mat, im->opsin_inv_mat);
}

MICROJXL__STATIC_RETURNS_ERR microjxl__image_metadata(microjxl__st *st) {
	static const float SRGB_CHROMA[4][2] = { // default chromacity (kD65, kSRGB)
		{0.3127f, 0.3290f}, {0.639998686f, 0.330010138f},
		{0.300003784f, 0.600003357f}, {0.150002046f, 0.059997204f},
	};
	static const float OPSIN_INV_MAT[3][3] = { // default opsin inverse matrix
		{11.031566901960783f, -9.866943921568629f, -0.16462299647058826f},
		{-3.254147380392157f, 4.418770392156863f, -0.16462299647058826f},
		{-3.6588512862745097f, 2.7129230470588235f, 1.9459282392156863f},
	};

	microjxl__image_st *im = st->image;
	int32_t i, j;

	im->orientation = MICROJXL__ORIENT_TL;
	im->intr_width = 0;
	im->intr_height = 0;
	im->preview_w = 0;
	im->preview_h = 0;
	im->bpp = 8;
	im->exp_bits = 0;
	im->anim_tps_num = 0;
	im->anim_tps_denom = 0;
	im->anim_nloops = 0;
	im->anim_have_timecodes = 0;
	im->icc = NULL;
	im->iccsize = 0;
	im->cspace = MICROJXL__CS_CHROMA;
	memcpy(im->cpoints, SRGB_CHROMA, sizeof SRGB_CHROMA);
	im->gamma_or_tf = MICROJXL__TF_SRGB;
	im->render_intent = MICROJXL__INTENT_REL;
	im->intensity_target = 255.0f;
	im->min_nits = 0.0f;
	im->linear_below = 0.0f;
	im->relative_to_max_display = 0.0f;
	im->render_spot = 1; // libjxl renders spot colours by default
	im->ec_canvas = NULL;
	im->num_ec_canvas = 0;
	im->modular_16bit_buffers = 1;
	im->xyb_encoded = 1;
	memcpy(im->opsin_inv_mat, OPSIN_INV_MAT, sizeof OPSIN_INV_MAT);
	/* The opsin absorbance bias is a positive constant in the spec
	 * (kOpsinAbsorbanceBias0 = +0.0037930732552754493), but the XYB->RGB
	 * conversion (libjxl XybToRgb, dec_xyb-inl.h) uses the *negative* of it
	 * for both the cbrt subtraction and the re-add:
	 *   gamma = (Y+-X) - cbrt(-bias) = (Y+-X) + cbrt(bias)
	 *   mixed = gamma^3 + (-bias) = gamma^3 - bias
	 * so the stored bias below is negative to match the reference output. */
	im->opsin_bias[0] = im->opsin_bias[1] = im->opsin_bias[2] = -0.0037930732552754493f;
	im->quant_bias[0] = 1.0f - 0.05465007330715401f;
	im->quant_bias[1] = 1.0f - 0.07005449891748593f;
	im->quant_bias[2] = 1.0f - 0.049935103337343655f;
	im->quant_bias_num = 0.145f;

	MICROJXL__TRY(microjxl__size_header(st, &im->width, &im->height));
	MICROJXL__SHOULD(im->width <= st->limits->width && im->height <= st->limits->height, "slim");
	MICROJXL__SHOULD((int64_t) im->width * im->height <= st->limits->pixels, "slim");

	if (!microjxl__u(st, 1)) { // !all_default
		int32_t extra_fields = microjxl__u(st, 1);
		if (extra_fields) {
			im->orientation = (enum microjxl__orientation) (microjxl__u(st, 3) + 1);
			if (microjxl__u(st, 1)) { // have_intr_size
				MICROJXL__TRY(microjxl__size_header(st, &im->intr_width, &im->intr_height));
			}
			if (microjxl__u(st, 1)) { // have_preview
#ifdef MICROJXL_DEBUG
				fprintf(stderr, "[microjxl] image_metadata: have_preview=1\n");
#endif
				/* D.3.3 PreviewHeader: same shape as SizeHeader (div8,
				 * height, ratio, width). The preview frame itself is a
				 * self-contained regular frame that follows the image
				 * metadata; microjxl skips it in microjxl_next_frame (a preview is
				 * purely an optimisation for progressive display, the
				 * main frame does not depend on it). */
				MICROJXL__TRY(microjxl__size_header(st, &im->preview_w, &im->preview_h));
				MICROJXL__SHOULD(im->preview_w > 0 && im->preview_h > 0 &&
					im->preview_w <= 4096 && im->preview_h <= 4096, "psiz");
			}
			if (microjxl__u(st, 1)) { // have_animation
				im->anim_tps_num = microjxl__u32(st, 100, 0, 1000, 0, 1, 10, 1, 30);
				im->anim_tps_denom = microjxl__u32(st, 1, 0, 1001, 0, 1, 8, 1, 10);
				im->anim_nloops = microjxl__64u32(st, 0, 0, 0, 3, 0, 16, 0, 32);
				im->anim_have_timecodes = microjxl__u(st, 1);
			}
		}
		MICROJXL__TRY(microjxl__bit_depth(st, &im->bpp, &im->exp_bits));
		MICROJXL__SHOULD(im->bpp <= st->limits->bpp, "fbpp");
		im->modular_16bit_buffers = microjxl__u(st, 1);
		MICROJXL__SHOULD(im->modular_16bit_buffers || !st->limits->needs_modular_16bit_buffers, "fm32");
		im->num_extra_channels = microjxl__u32(st, 0, 0, 1, 0, 2, 4, 1, 12);
		MICROJXL__SHOULD(im->num_extra_channels <= st->limits->num_extra_channels, "elim");
		MICROJXL__TRY_CALLOC(microjxl__ec_info, &im->ec_info, (size_t) im->num_extra_channels);
		for (i = 0; i < im->num_extra_channels; ++i) im->ec_info[i].name = NULL;
		for (i = 0; i < im->num_extra_channels; ++i) {
			microjxl__ec_info *ec = &im->ec_info[i];
			if (microjxl__u(st, 1)) { // d_alpha
				ec->type = MICROJXL__EC_ALPHA;
				ec->bpp = 8;
				ec->exp_bits = ec->dim_shift = ec->name_len = 0;
				ec->data.alpha_associated = 0;
			} else {
				ec->type = (enum microjxl__ec_type) microjxl__enum(st);
				MICROJXL__TRY(microjxl__bit_depth(st, &ec->bpp, &ec->exp_bits));
				ec->dim_shift = microjxl__u32(st, 0, 0, 3, 0, 4, 0, 1, 3);
				MICROJXL__TRY(microjxl__name(st, &ec->name_len, &ec->name));
				switch (ec->type) {
				case MICROJXL__EC_ALPHA:
					ec->data.alpha_associated = microjxl__u(st, 1);
					break;
				case MICROJXL__EC_SPOT_COLOUR:
					ec->data.spot.red = microjxl__f16(st);
					ec->data.spot.green = microjxl__f16(st);
					ec->data.spot.blue = microjxl__f16(st);
					ec->data.spot.solidity = microjxl__f16(st);
					break;
				case MICROJXL__EC_CFA:
					ec->data.cfa_channel = microjxl__u32(st, 1, 0, 0, 2, 3, 4, 19, 8);
					break;
				case MICROJXL__EC_BLACK:
					MICROJXL__SHOULD(st->limits->ec_black_allowed, "fblk");
					break;
				case MICROJXL__EC_DEPTH: case MICROJXL__EC_SELECTION_MASK:
				case MICROJXL__EC_THERMAL: case MICROJXL__EC_NON_OPTIONAL: case MICROJXL__EC_OPTIONAL:
					break;
				default: MICROJXL__RAISE("ect?");
				}
			}
			MICROJXL__SHOULD(ec->bpp <= st->limits->bpp, "fbpp");
			if (getenv("MICROJXL_TRACE_EC")) {
				fprintf(stderr, "[microjxl] EC%d type=%d bpp=%d exp=%d dim=%d", i, (int) ec->type, ec->bpp, ec->exp_bits, ec->dim_shift);
				if (ec->type == MICROJXL__EC_SPOT_COLOUR)
					fprintf(stderr, " spot=(%g,%g,%g) solidity=%g", ec->data.spot.red, ec->data.spot.green, ec->data.spot.blue, ec->data.spot.solidity);
				fprintf(stderr, "\n");
			}
			MICROJXL__RAISE_DELAYED();
		}
		im->xyb_encoded = microjxl__u(st, 1);
		if (!microjxl__u(st, 1)) { // ColourEncoding.all_default
			enum cspace { CS_RGB = 0, CS_GREY = 1, CS_XYB = 2, CS_UNKNOWN = 3 } cspace;
			enum { WP_D65 = 1, WP_CUSTOM = 2, WP_E = 10, WP_DCI = 11 };
			enum { PR_SRGB = 1, PR_CUSTOM = 2, PR_2100 = 9, PR_P3 = 11 };
			im->want_icc = microjxl__u(st, 1);
			cspace = (enum cspace) microjxl__enum(st);
			switch (cspace) {
			case CS_RGB: case CS_UNKNOWN: im->cspace = MICROJXL__CS_CHROMA; break;
			case CS_GREY: im->cspace = MICROJXL__CS_GREY; break;
			case CS_XYB: im->cspace = MICROJXL__CS_XYB; break;
			default: MICROJXL__RAISE("csp?");
			}
			// TODO: should verify cspace grayness with ICC grayness
			if (!im->want_icc) {
				if (cspace != CS_XYB) {
					static const float E[2] = {1/3.f, 1/3.f}, DCI[2] = {0.314f, 0.351f},
						BT2100[3][2] = {{0.708f, 0.292f}, {0.170f, 0.797f}, {0.131f, 0.046f}},
						P3[3][2] = {{0.680f, 0.320f}, {0.265f, 0.690f}, {0.150f, 0.060f}};
					switch (microjxl__enum(st)) {
					case WP_D65: break; // default
					case WP_CUSTOM: MICROJXL__TRY(microjxl__customxy(st, im->cpoints[MICROJXL__CHROMA_WHITE])); break;
					case WP_E: memcpy(im->cpoints + MICROJXL__CHROMA_WHITE, E, sizeof E); break;
					case WP_DCI: memcpy(im->cpoints + MICROJXL__CHROMA_WHITE, DCI, sizeof DCI); break;
					default: MICROJXL__RAISE("wpt?");
					}
					if (cspace != CS_GREY) {
						switch (microjxl__enum(st)) {
						case PR_SRGB: break; // default
						case PR_CUSTOM:
							MICROJXL__TRY(microjxl__customxy(st, im->cpoints[MICROJXL__CHROMA_RED]));
							MICROJXL__TRY(microjxl__customxy(st, im->cpoints[MICROJXL__CHROMA_GREEN]));
							MICROJXL__TRY(microjxl__customxy(st, im->cpoints[MICROJXL__CHROMA_BLUE]));
							break;
						case PR_2100: memcpy(im->cpoints + MICROJXL__CHROMA_RED, BT2100, sizeof BT2100); break;
						case PR_P3: memcpy(im->cpoints + MICROJXL__CHROMA_RED, P3, sizeof P3); break;
						default: MICROJXL__RAISE("prm?");
						}
					}
				}
				if (microjxl__u(st, 1)) { // have_gamma
					im->gamma_or_tf = microjxl__u(st, 24);
					MICROJXL__SHOULD(im->gamma_or_tf > 0 && im->gamma_or_tf <= MICROJXL__GAMMA_MAX, "gama");
					if (cspace == CS_XYB) MICROJXL__SHOULD(im->gamma_or_tf == 3333333, "gama");
				} else {
					im->gamma_or_tf = -microjxl__enum(st);
					MICROJXL__SHOULD((
						1 << -MICROJXL__TF_709 | 1 << -MICROJXL__TF_UNKNOWN | 1 << -MICROJXL__TF_LINEAR |
						1 << -MICROJXL__TF_SRGB | 1 << -MICROJXL__TF_PQ | 1 << -MICROJXL__TF_DCI |
						1 << -MICROJXL__TF_HLG
					) >> -im->gamma_or_tf & 1, "tfn?");
				}
				im->render_intent = (enum microjxl__render_intent) microjxl__enum(st);
				MICROJXL__SHOULD((
					1 << MICROJXL__INTENT_PERC | 1 << MICROJXL__INTENT_REL |
					1 << MICROJXL__INTENT_SAT | 1 << MICROJXL__INTENT_ABS
				) >> im->render_intent & 1, "itt?");
			}
		}
		if (extra_fields) {
			if (!microjxl__u(st, 1)) { // ToneMapping.all_default
				int relative_to_max_display;
				im->intensity_target = microjxl__f16(st);
				MICROJXL__SHOULD(im->intensity_target > 0, "tone");
				im->min_nits = microjxl__f16(st);
				/* libjxl validates 0 <= min_nits <= intensity_target; 0 is legal
				 * (it is the ToneMapping default) */
				MICROJXL__SHOULD(im->min_nits >= 0 && im->min_nits <= im->intensity_target, "tone");
				relative_to_max_display = microjxl__u(st, 1);
				im->relative_to_max_display = (float) relative_to_max_display;
				im->linear_below = microjxl__f16(st);
				if (relative_to_max_display) {
					MICROJXL__SHOULD(0 <= im->linear_below && im->linear_below <= 1, "tone");
					im->linear_below *= -1.0f;
				} else {
					MICROJXL__SHOULD(0 <= im->linear_below, "tone");
				}
			}
		}
		MICROJXL__TRY(microjxl__extensions(st));
	}
	if (!microjxl__u(st, 1)) { // !default_m
	int32_t cw_mask;
	if (im->xyb_encoded) {
		/* OpsinInverseMatrix is a nested bundle with its own all_default bit
		 * (libjxl image_metadata.cc OpsinInverseMatrix::VisitFields; spec
		 * "default_m and xyb encoded OpsinInverseMatrix opsin_inverse matrix"). */
		if (!microjxl__u(st, 1)) { // OpsinInverseMatrix.all_default
			for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) im->opsin_inv_mat[i][j] = microjxl__f16(st);
			for (i = 0; i < 3; ++i) im->opsin_bias[i] = microjxl__f16(st);
			for (i = 0; i < 3; ++i) im->quant_bias[i] = microjxl__f16(st);
			im->quant_bias_num = microjxl__f16(st);
		}
	}
		cw_mask = (uint32_t) microjxl__u(st, 3);
		if (cw_mask & 1) {
			for (i = 0; i < 15; ++i) im->up2_weight[i] = microjxl__f16(st);
		}
		if (cw_mask & 2) {
			for (i = 0; i < 55; ++i) im->up4_weight[i] = microjxl__f16(st);
		}
		if (cw_mask & 4) {
			for (i = 0; i < 210; ++i) im->up8_weight[i] = microjxl__f16(st);
		}
		im->up_cw_mask = cw_mask;
	}
	/* XYB output with non-sRGB primaries: fold srgb_to_original into the
	 * opsin inverse matrix (libjxl dec_xyb.cc SetColorEncoding); skipped for
	 * ICC files (no primaries parsed) and non-XYB streams */
	if (im->xyb_encoded && im->cspace == MICROJXL__CS_CHROMA && !im->want_icc &&
		memcmp(im->cpoints, SRGB_CHROMA, sizeof SRGB_CHROMA) != 0) {
		microjxl__fold_primaries_into_opsin_inv(im);
	}
	MICROJXL__RAISE_DELAYED();
	return 0;

MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC void microjxl__mem_free_image_state(microjxl__image_st *im) {
	int32_t i, r;
	if (im->ec_info) {
		for (i = 0; i < im->num_extra_channels; ++i) microjxl__mem_free(im->ec_info[i].name);
		microjxl__mem_free(im->ec_info);
		im->ec_info = NULL;
	}
	microjxl__mem_free(im->icc);
	im->icc = NULL;
	im->num_extra_channels = 0;
	for (r = 0; r < 4; ++r) {
		if (im->ref_planes[r]) {
			for (i = 0; i < im->ref_nch[r]; ++i) microjxl__mem_free_plane(&im->ref_planes[r][i]);
			microjxl__mem_free(im->ref_planes[r]);
			im->ref_planes[r] = NULL;
		}
		im->ref_nch[r] = 0;
		im->ref_present[r] = 0;
		microjxl__mem_free_plane(&im->ref_rgba[r]);
	}
	for (r = 0; r < 4; ++r) {
		for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&im->lf_frames[r][i]);
		im->lf_frame_present[r] = 0;
	}
	if (im->canvas) {
		microjxl__mem_free_plane(im->canvas);
		microjxl__mem_free(im->canvas);
		im->canvas = NULL;
	}
	if (im->ref_render) {
		microjxl__mem_free_plane(im->ref_render);
		microjxl__mem_free(im->ref_render);
		im->ref_render = NULL;
	}
	if (im->ec_canvas) {
		for (i = 0; i < im->num_ec_canvas; ++i) microjxl__mem_free_plane(&im->ec_canvas[i]);
		microjxl__mem_free(im->ec_canvas);
		im->ec_canvas = NULL;
		im->num_ec_canvas = 0;
	}
	for (r = 0; r < 4; ++r) {
		if (im->ref_ec[r]) {
			for (i = 0; i < im->ref_ec_n[r]; ++i) microjxl__mem_free_plane(&im->ref_ec[r][i]);
			microjxl__mem_free(im->ref_ec[r]);
			im->ref_ec[r] = NULL;
		}
		im->ref_ec_n[r] = 0;
	}
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// ICC

MICROJXL__STATIC_RETURNS_ERR microjxl__icc(microjxl__st *st);

/* Un-prediction of the encoded ICC profile (libjxl UnpredictICC,
 * icc_codec.cc + icc_codec_common.cc): the anEntropy-decoded byte stream
 * is a command language describing how to reconstruct the profile from the
 * initial header prediction, tag list and data runs. Storing the decoded
 * profile in im->icc (the "TODO actually interpret them" placeholder).
 * The conformance suite compares these profiles byte-exactly. */
#ifdef MICROJXL_IMPLEMENTATION

#define MICROJXL__ICC_HEADER_SIZE 128
#define MICROJXL__ICC_CMD_INSERT 1
#define MICROJXL__ICC_CMD_SHUFFLE2 2
#define MICROJXL__ICC_CMD_SHUFFLE4 3
#define MICROJXL__ICC_CMD_PREDICT 4
#define MICROJXL__ICC_CMD_XYZ 10
#define MICROJXL__ICC_CMD_TYPE_START 16
#define MICROJXL__ICC_FLAG_OFFSET 64
#define MICROJXL__ICC_FLAG_SIZE 128

static void microjxl__icc_shuffle(uint8_t *data, uint32_t size, uint32_t width) {
	uint8_t *result;
	uint32_t height = (size + width - 1) / width, i, s = 0, j = 0;
	/* height is at most 2^32; the allocation cannot overflow (size is
	 * bounded by the ICC output limit before this is called) */
	result = (uint8_t *) microjxl__malloc(size ? size : 1, 1);
	if (!result) return; // caller validates the output size, this degrades to garbage
	for (i = 0; i < size; i++) {
		result[i] = data[j];
		j += height;
		if (j >= size) j = ++s;
	}
	memcpy(data, result, size);
	microjxl__mem_free(result);
}

/* growable output byte buffer for the ICC un-prediction */
typedef struct {
	uint8_t *data;
	uint32_t size, cap;
	int oom; // an allocation failed; abort the reconstruction
	int varint_overflow; // a malformed >10-byte or >2^64 varint was seen
} microjxl__icc_dec;

/* Two base-128 varints lead the stream (libjxl DecodeVarInt: at most 10
 * bytes each, the 10th may only contribute bit 63). */
static uint64_t microjxl__icc_stream_varint(microjxl__icc_dec *d, const uint8_t *input, uint64_t inputsize, uint64_t *pos) {
	uint64_t ret = 0;
	int i;
	for (i = 0; i < 9; ++i) {
		uint8_t byte;
		if (*pos >= inputsize) { d->varint_overflow = 1; return 0; }
		byte = input[(*pos)++];
		ret |= (uint64_t) (byte & 0x7F) << (7 * i);
		if (!(byte & 0x80)) return ret;
	}
	if (*pos >= inputsize) { d->varint_overflow = 1; return 0; }
	{
		uint8_t byte = input[(*pos)++];
		if (byte & 0x80) { d->varint_overflow = 1; return 0; }
		if (byte & 0x7E) { d->varint_overflow = 1; return 0; } // exceeds 2^64-1
		ret |= (uint64_t) (byte & 0x01) << 63;
	}
	return ret;
}

/* libjxl kIccInitialHeaderPrediction (icc_codec_common.cc), 128 bytes. */
static const uint8_t MICROJXL__ICC_INITIAL_HEADER[MICROJXL__ICC_HEADER_SIZE] = {
	0,   0,   0,   0,   0,   0,   0,   0,   4, 0, 0, 0, 'm', 'n', 't', 'r',
	'R', 'G', 'B', ' ', 'X', 'Y', 'Z', ' ', 0, 0, 0, 0, 0,   0,   0,   0,
	0,   0,   0,   0,   'a', 'c', 's', 'p', 0, 0, 0, 0, 0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,   246, 214, 0, 1, 0, 0, 0,   0,   211, 45,
	0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,
	0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,
};

static void microjxl__icc_put32(microjxl__icc_dec *d, uint32_t v) {
	if (d->size + 4 > d->cap || !d->data) {
		uint32_t ncap = d->cap ? d->cap : 1024;
		uint8_t *np;
		while (d->size + 4 > ncap) ncap *= 2;
		np = (uint8_t *) MICROJXL_REALLOC(d->data, ncap);
		if (!np) { d->oom = 1; return; }
		d->data = np; d->cap = ncap;
	}
	d->data[d->size++] = (uint8_t) (v >> 24);
	d->data[d->size++] = (uint8_t) (v >> 16);
	d->data[d->size++] = (uint8_t) (v >> 8);
	d->data[d->size++] = (uint8_t) v;
}

static void microjxl__icc_put(microjxl__icc_dec *d, const uint8_t *buf, uint64_t n) {
	uint64_t i;
	if (d->oom) return;
	if (d->size + n > d->cap || !d->data) {
		uint32_t ncap = d->cap ? d->cap : 1024;
		uint8_t *np;
		while ((uint64_t) d->size + n > ncap) ncap *= 2;
		np = (uint8_t *) MICROJXL_REALLOC(d->data, ncap);
		if (!np) { d->oom = 1; return; }
		d->data = np; d->cap = ncap;
	}
	for (i = 0; i < n; ++i) d->data[d->size++] = buf[i];
}

static void microjxl__icc_putbyte(microjxl__icc_dec *d, uint8_t b) { microjxl__icc_put(d, &b, 1); }

static uint32_t microjxl__icc_get32(const uint8_t *data, uint64_t size, uint64_t pos) {
	if (pos + 4 > size) return 0;
	return ((uint32_t) data[pos] << 24) | ((uint32_t) data[pos + 1] << 16) | ((uint32_t) data[pos + 2] << 8) | data[pos + 3];
}

static uint32_t microjxl__icc_predict_value(uint32_t p1, uint32_t p2, uint32_t p3, int order) {
	if (order == 0) return p1;
	if (order == 1) return 2 * p1 - p2;
	if (order == 2) return 3 * p1 - 3 * p2 + p3;
	return 0;
}

static uint8_t microjxl__icc_linear_predict(const uint8_t *data, uint64_t start, uint64_t i, uint64_t stride, uint32_t width, int order) {
	uint64_t pos = start + i;
	if (width == 1) {
		uint8_t p1 = data[pos - stride], p2 = data[pos - stride * 2], p3 = data[pos - stride * 3];
		return (uint8_t) microjxl__icc_predict_value(p1, p2, p3, order);
	} else if (width == 2) {
		uint64_t p = start + (i & ~(uint64_t) 1);
		uint32_t p1 = ((uint32_t) data[p - stride] << 8) + data[p - stride + 1];
		uint32_t p2 = ((uint32_t) data[p - stride * 2] << 8) + data[p - stride * 2 + 1];
		uint32_t p3 = ((uint32_t) data[p - stride * 3] << 8) + data[p - stride * 3 + 1];
		uint32_t pred = microjxl__icc_predict_value(p1, p2, p3, order);
		return (uint8_t) ((i & 1) ? (pred & 255) : ((pred >> 8) & 255));
	} else {
		uint64_t p = start + (i & ~(uint64_t) 3);
		uint32_t p1 = microjxl__icc_get32(data, pos, p - stride);
		uint32_t p2 = microjxl__icc_get32(data, pos, p - stride * 2);
		uint32_t p3 = microjxl__icc_get32(data, pos, p - stride * 3);
		uint32_t pred = microjxl__icc_predict_value(p1, p2, p3, order);
		uint32_t shiftbytes = 3 - (uint32_t) (i & 3);
		return (uint8_t) ((pred >> (shiftbytes * 8)) & 255);
	}
}

static void microjxl__icc_predict_header(const uint8_t *icc, uint64_t size, uint8_t *header, uint64_t pos) {
	if (pos == 8 && size >= 8) {
		header[80] = icc[4]; header[81] = icc[5]; header[82] = icc[6]; header[83] = icc[7];
	}
	if (pos == 41 && size >= 41) {
		if (icc[40] == 'A') { header[41] = 'P'; header[42] = 'P'; header[43] = 'L'; }
		if (icc[40] == 'M') { header[41] = 'S'; header[42] = 'F'; header[43] = 'T'; }
	}
	if (pos == 42 && size >= 42) {
		if (icc[40] == 'S' && icc[41] == 'G') { header[42] = 'I'; header[43] = ' '; }
		if (icc[40] == 'S' && icc[41] == 'U') { header[42] = 'N'; header[43] = 'W'; }
	}
}

static const char *MICROJXL__ICC_TAG_STRINGS[17] = {
	"cprt", "wtpt", "bkpt", "rXYZ", "gXYZ", "bXYZ", "kXYZ", "rTRC",
	"gTRC", "bTRC", "kTRC", "chad", "desc", "chrm", "dmnd", "dmdd", "lumi",
};
static const char *MICROJXL__ICC_TYPE_STRINGS[8] = {
	"XYZ ", "desc", "text", "mluc", "para", "curv", "sf32", "gbd ",
};

/* Decodes the encoded profile; on success the bytes are in d->data with
 * d->size entries. Mirrors libjxl UnpredictICC (icc_codec.cc). */
static int microjxl__icc_unpredict(microjxl__icc_dec *d, const uint8_t *enc, uint64_t size) {
	uint64_t pos = 0, osize, csize, cpos, commands_end, numtags;
	uint8_t header[MICROJXL__ICC_HEADER_SIZE];
	uint64_t i, h;

	/* two varints (output size, commands size), each up to 10 bytes */
	d->varint_overflow = 0;
	osize = microjxl__icc_stream_varint(d, enc, size, &pos);
	if (d->varint_overflow) return 0;
	csize = microjxl__icc_stream_varint(d, enc, size, &pos);
	if (d->varint_overflow) return 0;
	if (osize > 0xffffffffu || csize > 0xffffffffu) return 0;
	if (pos + csize > size) return 0;
	cpos = pos;
	commands_end = cpos + csize;
	pos = commands_end; // data stream follows the commands

	/* header: initial prediction plus predicted-increment bytes */
	memset(header, 0, sizeof header);
	memcpy(header, MICROJXL__ICC_INITIAL_HEADER, sizeof MICROJXL__ICC_INITIAL_HEADER);
	header[0] = (uint8_t) (osize >> 24); header[1] = (uint8_t) (osize >> 16);
	header[2] = (uint8_t) (osize >> 8); header[3] = (uint8_t) osize;
	for (h = 0; h <= MICROJXL__ICC_HEADER_SIZE; ++h) {
		if (d->size == osize) {
			if (cpos != commands_end || pos != size) return 0;
			return 1; // valid end (profile shorter than the header)
		}
		if (h == MICROJXL__ICC_HEADER_SIZE) break;
		microjxl__icc_predict_header(d->data, d->size, header, h);
		if (pos >= size) return 0;
		microjxl__icc_putbyte(d, (uint8_t) (enc[pos++] + header[h]));
		if (d->oom) return 0;
	}
	if (cpos >= commands_end) return 0;

	numtags = microjxl__icc_stream_varint(d, enc, commands_end, &cpos);
	if (d->varint_overflow) return 0;
	if (numtags != 0) {
		uint64_t prevtagstart = MICROJXL__ICC_HEADER_SIZE + (numtags - 1) * 12, prevtagsize = 0;
		numtags--;
		microjxl__icc_put32(d, (uint32_t) numtags);
		if (d->oom) return 0;
		for (;;) {
			uint8_t command, tagcode;
			const char *tag = NULL;
			uint64_t tagstart;
			uint64_t tagsize = prevtagsize;
			if (d->size > osize || cpos > commands_end) return 0;
			if (cpos == commands_end) break;
			command = enc[cpos++];
			tagcode = command & 63;
			if (tagcode == 0) {
				break;
			} else if (tagcode == 1) { // kCommandTagUnknown: 4 raw keyword bytes
				if (pos + 4 > size) return 0;
				microjxl__icc_put(d, enc + pos, 4);
				pos += 4;
				if (d->oom) return 0;
				tag = ""; // already written
			} else if (tagcode == 2) { // kCommandTagTRC
				tag = "rTRC";
			} else if (tagcode == 3) { // kCommandTagXYZ
				tag = "rXYZ";
			} else {
				if (tagcode - 4 >= 17) return 0;
				tag = MICROJXL__ICC_TAG_STRINGS[tagcode - 4];
			}
			if (tagcode != 1) {
				microjxl__icc_put(d, (const uint8_t *) tag, 4);
				if (d->oom) return 0;
			}
			if (tagcode == 3) { // XYZ tags carry 20-byte payloads
				/* rXYZ/gXYZ/bXYZ/kXYZ/wtpt/bkpt/lumi all have size 20; the
				 * tagcode-3 case is rXYZ specifically (see above) */
			}
			if (strcmp(tag, "rXYZ") == 0 || strcmp(tag, "gXYZ") == 0 || strcmp(tag, "bXYZ") == 0 ||
				strcmp(tag, "kXYZ") == 0 || strcmp(tag, "wtpt") == 0 || strcmp(tag, "bkpt") == 0 ||
				strcmp(tag, "lumi") == 0) {
				tagsize = 20;
			}
			if (command & MICROJXL__ICC_FLAG_OFFSET) {
				tagstart = microjxl__icc_stream_varint(d, enc, commands_end, &cpos);
				if (d->varint_overflow) return 0;
			} else {
				if (prevtagstart > 0xffffffffu) return 0;
				tagstart = prevtagstart + prevtagsize;
			}
			if (tagstart > 0xffffffffu) return 0;
			microjxl__icc_put32(d, (uint32_t) tagstart);
			if (command & MICROJXL__ICC_FLAG_SIZE) {
				tagsize = microjxl__icc_stream_varint(d, enc, commands_end, &cpos);
				if (d->varint_overflow) return 0;
			}
			if (tagsize > 0xffffffffu) return 0;
			microjxl__icc_put32(d, (uint32_t) tagsize);
			prevtagstart = tagstart;
			prevtagsize = tagsize;

			if (tagcode == 2) { // TRC: replicate to gTRC/bTRC
				microjxl__icc_put(d, (const uint8_t *) "gTRC", 4);
				microjxl__icc_put32(d, (uint32_t) tagstart);
				microjxl__icc_put32(d, (uint32_t) tagsize);
				microjxl__icc_put(d, (const uint8_t *) "bTRC", 4);
				microjxl__icc_put32(d, (uint32_t) tagstart);
				microjxl__icc_put32(d, (uint32_t) tagsize);
				if (d->oom) return 0;
			}
			if (tagcode == 3) { // XYZ: replicate to gXYZ/bXYZ at +size offsets
				if (tagstart + tagsize * 2 > 0xffffffffu) return 0;
				microjxl__icc_put(d, (const uint8_t *) "gXYZ", 4);
				microjxl__icc_put32(d, (uint32_t) (tagstart + tagsize));
				microjxl__icc_put32(d, (uint32_t) tagsize);
				microjxl__icc_put(d, (const uint8_t *) "bXYZ", 4);
				microjxl__icc_put32(d, (uint32_t) (tagstart + tagsize * 2));
				microjxl__icc_put32(d, (uint32_t) tagsize);
				if (d->oom) return 0;
			}
		}
	}

	/* main content commands */
	for (;;) {
		uint8_t command;
		if (d->size > osize || cpos > commands_end) return 0;
		if (cpos == commands_end) break;
		command = enc[cpos++];
		if (command == MICROJXL__ICC_CMD_INSERT) {
			uint64_t num = microjxl__icc_stream_varint(d, enc, commands_end, &cpos);
			if (d->varint_overflow) return 0;
			if (pos + num > size) return 0;
			microjxl__icc_put(d, enc + pos, num);
			pos += num;
			if (d->oom) return 0;
		} else if (command == MICROJXL__ICC_CMD_SHUFFLE2 || command == MICROJXL__ICC_CMD_SHUFFLE4) {
			uint64_t num = microjxl__icc_stream_varint(d, enc, commands_end, &cpos);
			uint32_t w = (command == MICROJXL__ICC_CMD_SHUFFLE2) ? 2 : 4;
			uint8_t *shuffled;
			if (d->varint_overflow) return 0;
			if (pos + num > size) return 0;
			shuffled = (uint8_t *) microjxl__malloc(num ? (size_t) num : 1, 1);
			if (!shuffled) return 0;
			memcpy(shuffled, enc + pos, (size_t) num);
			microjxl__icc_shuffle(shuffled, (uint32_t) num, w);
			microjxl__icc_put(d, shuffled, num);
			microjxl__mem_free(shuffled);
			pos += num;
			if (d->oom) return 0;
		} else if (command == MICROJXL__ICC_CMD_PREDICT) {
			uint8_t flags;
			uint32_t width;
			int order;
			uint64_t stride, num, start;
			uint8_t *shuffled;
			if (cpos + 2 > commands_end) return 0;
			flags = enc[cpos++];
			width = (uint32_t) (flags & 3) + 1;
			if (width == 3) return 0;
			order = (int) ((flags & 12) >> 2);
			if (order == 3) return 0;
			stride = width;
			if (flags & 16) {
				stride = microjxl__icc_stream_varint(d, enc, commands_end, &cpos);
				if (d->varint_overflow) return 0;
				if (stride < width) return 0;
			}
			if (d->size == 0 || ((uint64_t) d->size - 1) >> 2 < stride) return 0;
			num = microjxl__icc_stream_varint(d, enc, commands_end, &cpos);
			if (d->varint_overflow) return 0;
			if (pos + num > size) return 0;
			shuffled = (uint8_t *) microjxl__malloc(num ? (size_t) num : 1, 1);
			if (!shuffled) return 0;
			memcpy(shuffled, enc + pos, (size_t) num);
			if (width > 1) microjxl__icc_shuffle(shuffled, (uint32_t) num, width);
			start = d->size;
			for (i = 0; i < num; ++i) {
				uint8_t predicted = microjxl__icc_linear_predict(d->data, start, i, stride, width, order);
				microjxl__icc_putbyte(d, (uint8_t) (predicted + shuffled[i]));
			}
			microjxl__mem_free(shuffled);
			pos += num;
			if (d->oom) return 0;
		} else if (command == MICROJXL__ICC_CMD_XYZ) {
			microjxl__icc_put(d, (const uint8_t *) "XYZ ", 4);
			microjxl__icc_put(d, (const uint8_t *) "\0\0\0\0", 4);
			if (pos + 12 > size) return 0;
			microjxl__icc_put(d, enc + pos, 12);
			pos += 12;
			if (d->oom) return 0;
		} else if (command >= MICROJXL__ICC_CMD_TYPE_START && command < MICROJXL__ICC_CMD_TYPE_START + 8) {
			const char *ts = MICROJXL__ICC_TYPE_STRINGS[command - MICROJXL__ICC_CMD_TYPE_START];
			microjxl__icc_put(d, (const uint8_t *) ts, 4);
			microjxl__icc_put(d, (const uint8_t *) "\0\0\0\0", 4);
			if (d->oom) return 0;
		} else {
			return 0;
		}
	}
	if (pos != size) return 0;
	return d->size == osize;
}

#endif // defined MICROJXL_IMPLEMENTATION

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__icc(microjxl__st *st) {
	uint64_t enc_size, output_size, index;
	microjxl__code_spec codespec = MICROJXL__INIT;
	microjxl__code_st code = MICROJXL__INIT;
	int32_t byte = 0, prev = 0, pprev = 0, ctx;
	uint8_t *encdata = NULL;
	microjxl__image_st *im = st->image;
	int ok = 0;

	enc_size = microjxl__u64(st);
	MICROJXL__TRY(microjxl__read_code_spec(st, 41, &codespec));
	microjxl__init_code(&code, &codespec);
	index = 0;
	/* The varint bytes are the first bytes of the encoded stream and are
	 * entropy coded like everything else; decode the leading output-size
	 * varint manually so every consumed byte is captured in encdata (the
	 * un-prediction parses the varints again from the flat buffer). */
	MICROJXL__TRY_MALLOC(uint8_t, &encdata, (size_t) enc_size + 1);
	{
		uint64_t value = 0;
		int32_t shift = 0, b;
		do {
			if ((uint64_t) index >= enc_size) MICROJXL__RAISE("icc?");
			b = microjxl__code(st, 0, 0, &code);
			MICROJXL__RAISE_DELAYED();
			encdata[index++] = (uint8_t) b;
			value |= (uint64_t) (b & 0x7f) << shift;
			if (b < 128) break;
			shift += 7;
			if (shift >= 63) MICROJXL__RAISE("vint");
		} while (1);
		output_size = value;
	}
	MICROJXL__SHOULD(output_size <= st->limits->icc_output_size, "plim");
	/* the encoded profile bytes must fit the output limit as well (the
	 * reconstruction reads from a flat buffer) */
	MICROJXL__SHOULD(enc_size <= st->limits->icc_output_size, "plim");

	// SPEC it is still possible that enc_size is too large while output_size is within the limit.
	// the current spec allows for an arbitrarily large enc_size for the fixed output_size, because
	// some commands can generate zero output bytes, but as libjxl never emits such commands and
	// already (wrongly) assumes that a single command byte can generate at least one output byte,
	// microjxl instead chose to forbid such commands. with this restriction in place valid enc_size
	// can never exceed 21 times output_size, so this is what we are checking for.
	MICROJXL__SHOULD(output_size >= enc_size / 21, "icc?");

	for (; index < enc_size; ++index) {
		pprev = prev;
		prev = byte;
		ctx = 0;
		if (index > 128) {
			if (prev < 16) ctx = prev < 2 ? prev + 3 : 5;
			else if (prev > 240) ctx = 6 + (prev == 255);
			else if (97 <= (prev | 32) && (prev | 32) <= 122) ctx = 1;
			else if (prev == 44 || prev == 46 || (48 <= prev && prev < 58)) ctx = 2;
			else ctx = 8;
			if (pprev < 16) ctx += 2 * 8;
			else if (pprev > 240) ctx += 3 * 8;
			else if (97 <= (pprev | 32) && (pprev | 32) <= 122) ctx += 0 * 8;
			else if (pprev == 44 || pprev == 46 || (48 <= pprev && pprev < 58)) ctx += 1 * 8;
			else ctx += 4 * 8;
		}
		byte = microjxl__code(st, ctx, 0, &code);
		//printf("%zd/%zd: %zd ctx=%d byte=%#x %c\n", index, enc_size, microjxl__bits_read(st), ctx, (int)byte, 0x20 <= byte && byte < 0x7f ? byte : ' '); fflush(stdout);
		MICROJXL__RAISE_DELAYED();
		encdata[index] = (uint8_t) byte;
	}
	MICROJXL__TRY(microjxl__finish_and_free_code(st, &code));
	microjxl__mem_free_code_spec(&codespec);

	/* Reconstruct the profile from the command stream (libjxl
	 * UnpredictICC) and store it on the image. A reconstruction failure
	 * means the stream was inconsistent; that's a decode error like any
	 * other malformed profile. */
	{
		microjxl__icc_dec dec;
		memset(&dec, 0, sizeof dec);
		ok = microjxl__icc_unpredict(&dec, encdata, (uint64_t) enc_size);
		microjxl__mem_free(encdata);
		encdata = NULL;
		if (ok) {
			im->icc = (char *) dec.data;
			im->iccsize = dec.size;
		} else {
			microjxl__mem_free(dec.data);
			MICROJXL__RAISE("icc?");
		}
	}

	return 0;

	//size_t commands_size = microjxl__varint(st);

	/*
	static const char PREDICTIONS[] = {
		'*', '*', '*', '*', 0, 0, 0, 0, 4, 0, 0, 0, 'm', 'n', 't', 'r',
		'R', 'G', 'B', ' ', 'X', 'Y', 'Z', ' ', 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 'a', 'c', 's', 'p', 0, '@', '@', '@', 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 246, 214, 0, 1, 0, 0, 0, 0, 211, 45,
		'#', '#', '#', '#',
	};
	char pred = i < sizeof(PREDICTIONS) ? PREDICTIONS[i] : 0;
	switch (pred) {
	case '*': pred = output_size[i]; break;	case '#': pred = header[i - 76]; break;
	case '@':
		switch (header[40]) {
		case 'A': pred = "APPL"[i - 40]; break;
		case 'M': pred = "MSFT"[i - 40]; break;
		case 'S':
			switch (i < 41 ? 0 : header[41]) {
			case 'G': pred = "SGI "[i - 40]; break;
			case 'U': pred = "SUNW"[i - 40]; break;
			}
			break;
		}
		break;
	}
	*/

	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_code(&code);
	microjxl__mem_free_code_spec(&codespec);
	microjxl__mem_free(encdata);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// MA tree

enum { MICROJXL__NUM_PRED = 14 };

typedef union {
	struct {
		int32_t prop; // < 0, ~prop is the property index (e.g. -1 = channel index)
		int32_t value;
		int32_t leftoff, rightoff; // relative to the current node
	} branch;
	struct {
		int32_t ctx; // >= 0
		int32_t predictor;
		int32_t offset, multiplier;
	} leaf;
} microjxl__tree_node;

MICROJXL__STATIC_RETURNS_ERR microjxl__tree(
	microjxl__st *st, int32_t max_tree_size, microjxl__tree_node **tree, microjxl__code_spec *codespec
);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__tree(
	microjxl__st *st, int32_t max_tree_size, microjxl__tree_node **tree, microjxl__code_spec *codespec
) {
	microjxl__code_st code = MICROJXL__INIT;
	microjxl__tree_node *t = NULL;
	int32_t tree_idx = 0, tree_cap = 8;
	int32_t ctx_id = 0, nodes_left = 1;
	int32_t depth = 0, nodes_upto_this_depth = 1;

	MICROJXL__ASSERT(max_tree_size <= (1 << 26)); // codestream limit; the actual limit should be smaller
	MICROJXL__TRY(microjxl__read_code_spec(st, 6, codespec));
	microjxl__init_code(&code, codespec);
	MICROJXL__TRY_MALLOC(microjxl__tree_node, &t, (size_t) tree_cap);
	while (nodes_left-- > 0) { // depth-first, left-to-right ordering
		microjxl__tree_node *n;
		int32_t prop, val, shift;

		// the beginning of new tree depth; all `nodes_left` nodes are in this depth at the moment
		if (tree_idx == nodes_upto_this_depth) {
			MICROJXL__SHOULD(++depth <= st->limits->tree_depth, "tlim");
			nodes_upto_this_depth += nodes_left + 1;
		}

		prop = microjxl__code(st, 1, 0, &code);
		MICROJXL__TRY_REALLOC32(microjxl__tree_node, &t, tree_idx + 1, &tree_cap);
		n = &t[tree_idx++];
		if (prop > 0) {
			n->branch.prop = -prop;
			n->branch.value = microjxl__unpack_signed(microjxl__code(st, 0, 0, &code));
			n->branch.leftoff = ++nodes_left;
			n->branch.rightoff = ++nodes_left;
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_NODES")) fprintf(stderr, "[microjxl-tree] %d: branch prop=%d val=%d bitpos=%lld\n", tree_idx - 1, prop - 1, n->branch.value, (long long) microjxl__bits_read(st));
#endif
		} else {
			n->leaf.ctx = ctx_id++;
			n->leaf.predictor = microjxl__code(st, 2, 0, &code);
			{
				int32_t offtok = microjxl__code(st, 3, 0, &code);
				#ifdef MICROJXL_DEBUG
				if (getenv("MICROJXL_TOK3")) fprintf(stderr, "[tok3] leaf#%d ctx3tok=%u\n", n->leaf.ctx, (uint32_t) offtok);
				#endif
				n->leaf.offset = microjxl__unpack_signed(offtok);
			}
			shift = microjxl__code(st, 4, 0, &code);
			MICROJXL__SHOULD(shift < 31, "tree");
			val = microjxl__code(st, 5, 0, &code);
			MICROJXL__SHOULD(((val + 1) >> (31 - shift)) == 0, "tree");
			n->leaf.multiplier = (val + 1) << shift;
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_NODES")) fprintf(stderr, "[microjxl-tree] %d: leaf ctx=%d pred=%d off=%d mult=%d bitpos=%lld\n", tree_idx - 1, n->leaf.ctx, n->leaf.predictor, n->leaf.offset, n->leaf.multiplier, (long long) microjxl__bits_read(st));
#endif
		}

#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_DUMP_TREE") && prop > 0) {
			FILE *tf = fopen("/tmp/microjxltest/treeprops.txt", "a");
			fprintf(tf, "%d\n", prop - 1);
			fclose(tf);
		}
#endif
		MICROJXL__SHOULD(tree_idx + nodes_left <= max_tree_size, "tlim");
	}
	MICROJXL__ASSERT(tree_idx == nodes_upto_this_depth);
	MICROJXL__TRY(microjxl__finish_and_free_code(st, &code));

	microjxl__mem_free_code_spec(codespec);
	memset(codespec, 0, sizeof(*codespec)); // XXX is it required?
#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[microjxl] tree: ctx_id=%d nodes=%d bitpos=%lld\n", ctx_id, tree_idx, (long long) microjxl__bits_read(st));
#endif
	MICROJXL__TRY(microjxl__read_code_spec(st, ctx_id, codespec));
	#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_MLEAF") && codespec->cluster_map) {
		int32_t li;
		for (li = 0; li < ctx_id; ++li) {
			/* find leaf index li (leaves are in creation order = ctx id) */
			fprintf(stderr, "[mleaf] id=%d cmap=%d\n", li, (int) codespec->cluster_map[li]);
		}
	}
	#endif
	*tree = t;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(t);
	microjxl__mem_free_code(&code);
	microjxl__mem_free_code_spec(codespec);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// modular header

enum microjxl__transform_id {
	MICROJXL__TR_RCT = 0, MICROJXL__TR_PALETTE = 1, MICROJXL__TR_SQUEEZE = 2
};

typedef union {
	enum microjxl__transform_id tr;
	struct {
		enum microjxl__transform_id tr; // = MICROJXL__TR_RCT
		int32_t begin_c, type;
	} rct;
	struct {
		enum microjxl__transform_id tr; // = MICROJXL__TR_PALETTE
		int32_t begin_c, num_c, nb_colours, nb_deltas, d_pred;
	} pal;
	// this is nested in the bitstream, but flattened here.
	// nb_transforms get updated accordingly, but should be enough (the maximum is 80808)
	struct {
		enum microjxl__transform_id tr; // = MICROJXL__TR_SQUEEZE
		int implicit; // if true, no explicit parameters given in the bitstream
		int horizontal, in_place;
		int32_t begin_c, num_c;
	} sq;
} microjxl__transform;

typedef struct { int8_t p1, p2, p3[5], w[4]; } microjxl__wp_params;

typedef struct {
	int use_global_tree;
	microjxl__wp_params wp;
	int32_t nb_transforms;
	microjxl__transform *transform;
	microjxl__tree_node *tree; // owned only if use_global_tree is false
	microjxl__code_spec codespec;
	microjxl__code_st code;
	int32_t num_channels, nb_meta_channels;
	microjxl__plane *channel; // should use the same type, either i16 or i32
	int32_t dist_mult; // min(max(non-meta channel width), MICROJXL__MAX_DIST_MULT)
} microjxl__modular;

MICROJXL_STATIC void microjxl__init_modular_common(microjxl__modular *m);
MICROJXL__STATIC_RETURNS_ERR microjxl__init_modular(
	microjxl__st *st, int32_t num_channels, const int32_t *w, const int32_t *h, microjxl__modular *m
);
MICROJXL__STATIC_RETURNS_ERR microjxl__init_modular_for_global(
	microjxl__st *st, int frame_is_modular, int frame_do_ycbcr,
	int32_t frame_log_upsampling, const int32_t *frame_ec_log_upsampling,
	int32_t frame_width, int32_t frame_height, microjxl__modular *m
);
MICROJXL__STATIC_RETURNS_ERR microjxl__init_modular_for_pass_group(
	microjxl__st *st, int32_t num_gm_channels, int32_t gdim, int32_t gx0, int32_t gy0,
	int32_t minshift, int32_t maxshift, const microjxl__modular *gm, microjxl__modular *m
);
MICROJXL_STATIC void microjxl__combine_modular_from_pass_group(
	int32_t num_gm_channels, int32_t gdim, int32_t gy, int32_t gx,
	int32_t minshift, int32_t maxshift, const microjxl__modular *gm, microjxl__modular *m
);
MICROJXL__STATIC_RETURNS_ERR microjxl__init_modular_for_lf_group(
	microjxl__st *st, int32_t num_gm_channels, int32_t ggdim, int32_t ggx, int32_t ggy,
	const microjxl__modular *gm, microjxl__modular *m
);
MICROJXL_STATIC void microjxl__combine_modular_from_lf_group(
	int32_t num_gm_channels, int32_t ggdim, int32_t ggy, int32_t ggx,
	const microjxl__modular *gm, microjxl__modular *m
);
MICROJXL__STATIC_RETURNS_ERR microjxl__modular_header(
	microjxl__st *st, microjxl__tree_node *global_tree, const microjxl__code_spec *global_codespec,
	microjxl__modular *m
);
MICROJXL__STATIC_RETURNS_ERR microjxl__allocate_modular(microjxl__st *st, microjxl__modular *m);
MICROJXL_STATIC void microjxl__mem_free_modular(microjxl__modular *m);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_STATIC void microjxl__init_modular_common(microjxl__modular *m) {
	m->transform = NULL;
	m->tree = NULL;
	memset(&m->codespec, 0, sizeof(microjxl__code_spec));
	memset(&m->code, 0, sizeof(microjxl__code_st));
	m->code.spec = &m->codespec;
	m->channel = NULL;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__init_modular(
	microjxl__st *st, int32_t num_channels, const int32_t *w, const int32_t *h, microjxl__modular *m
) {
	int32_t i;

	microjxl__init_modular_common(m);
	m->num_channels = num_channels;
	MICROJXL__ASSERT(num_channels > 0);
	MICROJXL__TRY_CALLOC(microjxl__plane, &m->channel, (size_t) num_channels);
	for (i = 0; i < num_channels; ++i) {
		m->channel[i].width = w[i];
		m->channel[i].height = h[i];
		m->channel[i].hshift = m->channel[i].vshift = 0;
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__init_modular_for_global(
	microjxl__st *st, int frame_is_modular, int frame_do_ycbcr,
	int32_t frame_log_upsampling, const int32_t *frame_ec_log_upsampling,
	int32_t frame_width, int32_t frame_height, microjxl__modular *m
) {
	microjxl__image_st *im = st->image;
	int32_t i;

	microjxl__init_modular_common(m);
	m->num_channels = im->num_extra_channels;
	if (frame_is_modular) { // SPEC the condition is negated
		m->num_channels += (!frame_do_ycbcr && !im->xyb_encoded && im->cspace == MICROJXL__CS_GREY ? 1 : 3);
	}
	if (m->num_channels == 0) return 0;

	MICROJXL__TRY_CALLOC(microjxl__plane, &m->channel, (size_t) m->num_channels);
	for (i = 0; i < im->num_extra_channels; ++i) {
		/* The total upsampling of an extra channel is its frame-header
		 * upsampling plus its dim_shift, and it is stored at the frame's
		 * stored size divided by the difference (libjxl: DivCeil of the
		 * upsampled size by the channel's own factor). It must be at least
		 * the frame's upsampling. */
		int32_t log_upsampling = (frame_ec_log_upsampling ? frame_ec_log_upsampling[i] : 0) + im->ec_info[i].dim_shift;
		MICROJXL__SHOULD(log_upsampling >= frame_log_upsampling, "usmp");
		m->channel[i].width = microjxl__ceil_div32(frame_width, 1 << (log_upsampling - frame_log_upsampling));
		m->channel[i].height = microjxl__ceil_div32(frame_height, 1 << (log_upsampling - frame_log_upsampling));
		m->channel[i].hshift = m->channel[i].vshift = 0;
	}
	for (; i < m->num_channels; ++i) {
		m->channel[i].width = frame_width;
		m->channel[i].height = frame_height;
		m->channel[i].hshift = m->channel[i].vshift = 0;
	}
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(m->channel);
	m->channel = NULL;
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__init_modular_for_pass_group(
	microjxl__st *st, int32_t num_gm_channels, int32_t gdim, int32_t ggx, int32_t ggy,
	int32_t minshift, int32_t maxshift, const microjxl__modular *gm, microjxl__modular *m
) {
	int32_t i, max_channels;

	microjxl__init_modular_common(m);
	m->num_channels = 0;
	max_channels = gm->num_channels - num_gm_channels;
	MICROJXL__ASSERT(max_channels >= 0);
	MICROJXL__TRY_CALLOC(microjxl__plane, &m->channel, (size_t) max_channels);
	for (i = num_gm_channels; i < gm->num_channels; ++i) {
		microjxl__plane *gc = &gm->channel[i], *c = &m->channel[m->num_channels];
		if (gc->hshift < 3 || gc->vshift < 3) {
			MICROJXL__ASSERT(gc->hshift >= 0 && gc->vshift >= 0);
			/* libjxl dec_modular.cc DecodeGroup: a channel belongs to this
			 * pass iff min(hshift, vshift) is within the closed downsampling
			 * bracket [minshift, maxshift] for the pass. */
			int32_t shift = gc->hshift < gc->vshift ? gc->hshift : gc->vshift;
			if (shift < minshift || shift > maxshift) continue;
			/* libjxl computes each channel's decode rect from the UNCLAMPED
			 * group rect: Rect(x0>>hs, y0>>vs, group_dim>>hs, group_dim>>vs,
			 * gc->width, gc->height) -- clamped against the CHANNEL's extent
			 * from the shifted group origin, not against the frame. Using the
			 * frame-clamped group size here under-allocates the last group
			 * row/column whenever a shift is nonzero and desynchronizes the
			 * ANS stream for the rest of the group. */
			c->hshift = gc->hshift;
			c->vshift = gc->vshift;
			c->width = microjxl__min32(gdim >> gc->hshift, gc->width - (ggx >> gc->hshift));
			c->height = microjxl__min32(gdim >> gc->vshift, gc->height - (ggy >> gc->vshift));
			/* libjxl skips channels whose group rect is empty entirely */
			if (c->width <= 0 || c->height <= 0) continue;
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_PG")) fprintf(stderr, "[jpg] i=%d gc=%dx%d hs=%d vs=%d -> %dx%d hs=%d vs=%d ggx=%d ggy=%d\n", i, gc->width, gc->height, gc->hshift, gc->vshift, c->width, c->height, c->hshift, c->vshift, ggx, ggy);
#endif
			++m->num_channels;
		}
	}
	if (m->num_channels == 0) {
		microjxl__mem_free(m->channel);
		m->channel = NULL;
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC void microjxl__combine_modular_from_pass_group(
	int32_t num_gm_channels, int32_t gdim, int32_t ggx, int32_t ggy,
	int32_t minshift, int32_t maxshift, const microjxl__modular *gm, microjxl__modular *m
) {
	int32_t gcidx, cidx, y, gx0, gy0;
	(void) gdim;
	for (gcidx = num_gm_channels, cidx = 0; gcidx < gm->num_channels; ++gcidx) {
		microjxl__plane *gc = &gm->channel[gcidx], *c = &m->channel[cidx];
		MICROJXL__ASSERT(gc->type == c->type);
		if (gc->hshift < 3 || gc->vshift < 3) {
			/* same closed-bracket filter as init (see there for the reference);
			 * init guarantees m contains exactly the passing channels, with
			 * per-channel clamped rects; skip the empty ones like init does */
			int32_t shift = gc->hshift < gc->vshift ? gc->hshift : gc->vshift;
			if (shift < minshift || shift > maxshift) continue;
			if (c->width <= 0 || c->height <= 0) {
				++cidx;
				continue;
			}
			size_t pixel_size = (size_t) MICROJXL__PLANE_PIXEL_SIZE(gc);
			size_t gc_stride = (size_t) gc->stride_bytes, c_stride = (size_t) c->stride_bytes;
			MICROJXL__ASSERT(gc->hshift == c->hshift && gc->vshift == c->vshift);
			gx0 = ggx >> gc->hshift;
			gy0 = ggy >> gc->vshift;
			MICROJXL__ASSERT(gx0 + c->width <= gc->width && gy0 + c->height <= gc->height);
			for (y = 0; y < c->height; ++y) {
				memcpy(
					(void*) (gc->pixels + gc_stride * (size_t) (gy0 + y) + pixel_size * (size_t) gx0),
					(void*) (c->pixels + c_stride * (size_t) y),
					pixel_size * (size_t) c->width);
			}
			++cidx;
		}
	}
	MICROJXL__ASSERT(cidx == m->num_channels);
}

/* libjxl dec_frame.cc ProcessDCGroup: the ModularDC stream of an LF group
 * carries the modular channels with min(hshift, vshift) >= 3 (i.e. small
 * channels, e.g. the deep end of a squeeze pyramid), decoded over the
 * unclamped DC-group rect: (group_dim << 3) x (group_dim << 3) pixels at
 * the group origin. Pass groups carry the remaining channels (bracket
 * [0, 2]). Channel rects are clamped against the channel extent like
 * init_modular_for_pass_group does. */
MICROJXL__STATIC_RETURNS_ERR microjxl__init_modular_for_lf_group(
	microjxl__st *st, int32_t num_gm_channels, int32_t ggdim, int32_t ggx, int32_t ggy,
	const microjxl__modular *gm, microjxl__modular *m
) {
	int32_t i, max_channels;

	microjxl__init_modular_common(m);
	m->num_channels = 0;
	max_channels = gm->num_channels - num_gm_channels;
	MICROJXL__ASSERT(max_channels >= 0);
	MICROJXL__TRY_CALLOC(microjxl__plane, &m->channel, (size_t) max_channels);
	for (i = num_gm_channels; i < gm->num_channels; ++i) {
		microjxl__plane *gc = &gm->channel[i], *c = &m->channel[m->num_channels];
		if (gc->hshift >= 3 && gc->vshift >= 3) {
			/* libjxl DecodeGroup: shift = min(hshift, vshift); DC bracket is
			 * [3, 1000] so only channels with both shifts >= 3 qualify. */
			c->hshift = gc->hshift;
			c->vshift = gc->vshift;
			c->width = microjxl__min32(ggdim >> gc->hshift, gc->width - (ggx >> gc->hshift));
			c->height = microjxl__min32(ggdim >> gc->vshift, gc->height - (ggy >> gc->vshift));
			/* libjxl skips channels whose group rect is empty entirely */
			if (c->width <= 0 || c->height <= 0) continue;
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_PG")) fprintf(stderr, "[jlg] i=%d gc=%dx%d hs=%d vs=%d -> %dx%d ggx=%d ggy=%d\n", i, gc->width, gc->height, gc->hshift, gc->vshift, c->width, c->height, ggx, ggy);
#endif
			++m->num_channels;
		}
	}
	if (m->num_channels == 0) {
		microjxl__mem_free(m->channel);
		m->channel = NULL;
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC void microjxl__combine_modular_from_lf_group(
	int32_t num_gm_channels, int32_t ggdim, int32_t ggy, int32_t ggx,
	const microjxl__modular *gm, microjxl__modular *m
) {
	int32_t gcidx, cidx, y, gx0, gy0;
	(void) ggdim;
	for (gcidx = num_gm_channels, cidx = 0; gcidx < gm->num_channels; ++gcidx) {
		microjxl__plane *gc = &gm->channel[gcidx], *c;
		if (gc->hshift >= 3 && gc->vshift >= 3) {
			/* mirror init: channels whose group rect is empty were dropped
			 * there and must be skipped here without consuming an m slot */
			gx0 = ggx >> gc->hshift;
			gy0 = ggy >> gc->vshift;
			if (ggdim >> gc->hshift <= 0 || ggdim >> gc->vshift <= 0) continue;
			if (gc->width - gx0 <= 0 || gc->height - gy0 <= 0) continue;
			MICROJXL__ASSERT(cidx < m->num_channels);
			c = &m->channel[cidx];
			{
				size_t pixel_size = (size_t) MICROJXL__PLANE_PIXEL_SIZE(gc);
				size_t gc_stride = (size_t) gc->stride_bytes, c_stride = (size_t) c->stride_bytes;
				MICROJXL__ASSERT(gc->hshift == c->hshift && gc->vshift == c->vshift);
				MICROJXL__ASSERT(gx0 + c->width <= gc->width && gy0 + c->height <= gc->height);
				for (y = 0; y < c->height; ++y) {
					memcpy(
						(void*) (gc->pixels + gc_stride * (size_t) (gy0 + y) + pixel_size * (size_t) gx0),
						(void*) (c->pixels + c_stride * (size_t) y),
						pixel_size * (size_t) c->width);
				}
			}
			++cidx;
		}
	}
	MICROJXL__ASSERT(cidx == m->num_channels);
}

// H.6.2.1: default squeeze parameters when none are signalled (num_sq == 0).
// The steps are written into `out` (capacity `out_cap`, will be realloc'd if needed),
// `*out_n` gets the number of steps written, and `*out_first` the index of the first
// step in `out`. Only used from microjxl__modular_header.
MICROJXL__STATIC_RETURNS_ERR microjxl__squeeze_defaults(
	microjxl__st *st, int32_t nb_meta_channels, int32_t num_channels, const microjxl__plane *channel,
	microjxl__transform **out, int32_t out_first, int32_t *out_n, int32_t *out_cap
) {
	// spec H.6.2.1: default squeeze parameters (kMaxFirstPreviewSize == 8)
	int32_t first = nb_meta_channels, count = num_channels - first;
	int32_t w = channel[first].width, h = channel[first].height;
	microjxl__transform tmp[64];
	int32_t n = 0, i;

	MICROJXL__ASSERT(count > 0 && w > 0 && h > 0);

	// chroma channels are squeezed first (not in place)
	if (count > 2 && channel[first + 1].width == w && channel[first + 1].height == h) {
		tmp[n].sq.tr = MICROJXL__TR_SQUEEZE;
		tmp[n].sq.implicit = 0;
		tmp[n].sq.horizontal = 1;
		tmp[n].sq.in_place = 0;
		tmp[n].sq.begin_c = first + 1;
		tmp[n].sq.num_c = 2;
		++n;
		tmp[n] = tmp[n - 1];
		tmp[n].sq.horizontal = 0;
		++n;
	}

	// then the main squeeze, alternating horizontal/vertical, in place
	{
		int32_t step = n;
		tmp[n].sq.tr = MICROJXL__TR_SQUEEZE;
		tmp[n].sq.implicit = 0;
		tmp[n].sq.in_place = 1;
		tmp[n].sq.begin_c = first;
		tmp[n].sq.num_c = count;
		if (!(w > h)) { // h >= w
			if (h > 8) {
				tmp[n].sq.horizontal = 0;
				++n;
				h = (h + 1) / 2;
			}
		}
		while (w > 8 || h > 8) {
			if (w > 8) {
				tmp[n] = tmp[step];
				tmp[n].sq.horizontal = 1;
				++n;
				w = (w + 1) / 2;
			}
			if (h > 8) {
				tmp[n] = tmp[step];
				tmp[n].sq.horizontal = 0;
				++n;
				h = (h + 1) / 2;
			}
		}
	}
	// n may be 0 (image already <= 8x8): the squeeze entry then vanishes entirely

	MICROJXL__TRY_REALLOC32(microjxl__transform, out, out_first + n, out_cap);
	for (i = 0; i < n; ++i) (*out)[out_first + i] = tmp[i];
	*out_n = n;
MICROJXL__ON_ERROR:
	return st->err;
}

// H.6.2.1: forward channel effects for a single squeeze step. Halves the dimensions of
// channels [begin_c, end_c] and inserts the residual channel right after the squeezed
// channels (in_place) or at the end of the channel list (otherwise).
MICROJXL__STATIC_RETURNS_ERR microjxl__squeeze_forward(
	microjxl__st *st, microjxl__plane **channel, int32_t *num_channels, int32_t *nb_meta_channels,
	int32_t *channel_cap, const microjxl__transform *tr
) {
	int32_t begin_c = tr->sq.begin_c, num_c = tr->sq.num_c, end_c = begin_c + num_c - 1;
	int32_t offset = tr->sq.in_place ? end_c + 1 : *num_channels;
	int32_t c, w, h;
	microjxl__plane residu;

	MICROJXL__SHOULD(begin_c >= 0 && end_c < *num_channels, "sqcv");
	MICROJXL__SHOULD(*num_channels > 0, "sqcv");

	if (begin_c < *nb_meta_channels) {
		// spec: meta channels require in-place residuals
		MICROJXL__SHOULD(tr->sq.in_place, "sqme");
		MICROJXL__SHOULD(end_c < *nb_meta_channels, "sqme");
		*nb_meta_channels += num_c;
	}

	for (c = begin_c; c <= end_c; ++c) {
		w = (*channel)[c].width;
		h = (*channel)[c].height;
		MICROJXL__SHOULD(w > 0 && h > 0, "sqem");
		if (tr->sq.horizontal) {
			(*channel)[c].width = (w + 1) / 2;
			if ((*channel)[c].hshift >= 0) (*channel)[c].hshift++;
			w = w - (w + 1) / 2; // residual width
		} else {
			(*channel)[c].height = (h + 1) / 2;
			if ((*channel)[c].vshift >= 0) (*channel)[c].vshift++;
			h = h - (h + 1) / 2; // residual height
		}
		residu.type = 0; // unallocated; microjxl__allocate_modular will allocate it
		residu.misalign = 0;
		residu.vshift = (*channel)[c].vshift;
		residu.hshift = (*channel)[c].hshift;
		residu.width = w;
		residu.height = h;
		residu.stride_bytes = 0;
		residu.pixels = 0;
		MICROJXL__TRY_REALLOC32(microjxl__plane, channel, *num_channels + 1, channel_cap);
		// insert residu at index offset + (c - begin_c)
		memmove(*channel + offset + 1, *channel + offset,
			sizeof(**channel) * (size_t) (*num_channels - offset));
		(*channel)[offset] = residu;
		++*num_channels;
		++offset;
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__modular_header(
	microjxl__st *st, microjxl__tree_node *global_tree, const microjxl__code_spec *global_codespec,
	microjxl__modular *m
) {
	microjxl__plane *channel = m->channel;
	int32_t num_channels = m->num_channels, nb_meta_channels = 0;
	// note: channel_cap is the upper bound of # channels during inverse transform, and since
	// we don't shrink the channel list we don't ever need reallocation in microjxl__inverse_transform!
	int32_t channel_cap = m->num_channels, transform_cap;
	int32_t i, j;

	MICROJXL__ASSERT(num_channels > 0);

	m->use_global_tree = microjxl__u(st, 1);
	MICROJXL__SHOULD(!m->use_global_tree || global_tree, "mtre");

	{ // WPHeader
		int default_wp = microjxl__u(st, 1);
		m->wp.p1 = default_wp ? 16 : (int8_t) microjxl__u(st, 5);
		m->wp.p2 = default_wp ? 10 : (int8_t) microjxl__u(st, 5);
		for (i = 0; i < 5; ++i) m->wp.p3[i] = default_wp ? 7 * (i < 3) : (int8_t) microjxl__u(st, 5);
		for (i = 0; i < 4; ++i) m->wp.w[i] = default_wp ? 12 + (i < 1) : (int8_t) microjxl__u(st, 4);
	}

	transform_cap = m->nb_transforms = microjxl__u32(st, 0, 0, 1, 0, 2, 4, 18, 8);
	MICROJXL__SHOULD(m->nb_transforms <= st->limits->nb_transforms, "xlim");
	MICROJXL__TRY_MALLOC(microjxl__transform, &m->transform, (size_t) transform_cap);
	for (i = 0; i < m->nb_transforms; ++i) {
		microjxl__transform *tr = &m->transform[i];
		int32_t num_sq;

		tr->tr = (enum microjxl__transform_id) microjxl__u(st, 2);
		switch (tr->tr) {
		// RCT: [begin_c, begin_c+3) -> [begin_c, begin_c+3)
		case MICROJXL__TR_RCT: {
			int32_t begin_c = tr->rct.begin_c = microjxl__u32(st, 0, 3, 8, 6, 72, 10, 1096, 13);
			int32_t type = tr->rct.type = microjxl__u32(st, 6, 0, 0, 2, 2, 4, 10, 6);
			MICROJXL__SHOULD(type < 42, "rctt");
			MICROJXL__SHOULD(begin_c + 3 <= num_channels, "rctc");
			MICROJXL__SHOULD(begin_c >= nb_meta_channels || begin_c + 3 <= nb_meta_channels, "rctc");
			MICROJXL__SHOULD(microjxl__plane_all_equal_sized(channel + begin_c, channel + begin_c + 3), "rtcd");
			break;
		}

		// Palette: [begin_c, end_c) -> palette 0 (meta, nb_colours by num_c) + index begin_c+1
		case MICROJXL__TR_PALETTE: {
			microjxl__plane input;
			int32_t begin_c = tr->pal.begin_c = microjxl__u32(st, 0, 3, 8, 6, 72, 10, 1096, 13);
			int32_t num_c = tr->pal.num_c = microjxl__u32(st, 1, 0, 3, 0, 4, 0, 1, 13);
			int32_t end_c = begin_c + num_c;
			int32_t nb_colours = tr->pal.nb_colours = microjxl__u32(st, 0, 8, 256, 10, 1280, 12, 5376, 16);
			tr->pal.nb_deltas = microjxl__u32(st, 0, 0, 1, 8, 257, 10, 1281, 16);
			tr->pal.d_pred = microjxl__u(st, 4);
			MICROJXL__SHOULD(tr->pal.d_pred < MICROJXL__NUM_PRED, "palp");
			MICROJXL__SHOULD(end_c <= num_channels, "palc");
			if (begin_c < nb_meta_channels) { // num_c meta channels -> 2 meta channels (palette + index)
				MICROJXL__SHOULD(end_c <= nb_meta_channels, "palc");
				nb_meta_channels += 2 - num_c;
			} else { // num_c color channels -> 1 meta channel (palette) + 1 color channel (index)
				nb_meta_channels += 1;
			}
			MICROJXL__SHOULD(microjxl__plane_all_equal_sized(channel + begin_c, channel + end_c), "pald");
			// inverse palette transform always requires one more channel slot
			MICROJXL__TRY_REALLOC32(microjxl__plane, &channel, num_channels + 1, &channel_cap);
			input = channel[begin_c];
			memmove(channel + 1, channel, sizeof(*channel) * (size_t) begin_c);
			memmove(channel + begin_c + 2, channel + end_c, sizeof(*channel) * (size_t) (num_channels - end_c));
			channel[0].width = nb_colours;
			channel[0].height = num_c;
			channel[0].hshift = 0; // SPEC missing
			channel[0].vshift = -1;
			channel[begin_c + 1] = input;
			num_channels += 2 - num_c;
			break;
		}

		// Squeeze: halve the dimensions of [begin_c, end_c] and insert residual channels.
		case MICROJXL__TR_SQUEEZE: {
			num_sq = microjxl__u32(st, 0, 0, 1, 4, 9, 6, 41, 8);
			if (num_sq == 0) {
				MICROJXL__TRY(microjxl__squeeze_defaults(st, nb_meta_channels, num_channels, channel, &m->transform, i, &num_sq, &transform_cap));
				i += num_sq - 1;
				m->nb_transforms += num_sq - 1;
			} else {
				MICROJXL__TRY_REALLOC32(microjxl__transform, &m->transform, m->nb_transforms + num_sq - 1, &transform_cap);
				for (j = 0; j < num_sq; ++j) {
					tr = &m->transform[i + j];
					tr->sq.tr = MICROJXL__TR_SQUEEZE;
					tr->sq.implicit = 0;
					tr->sq.horizontal = microjxl__u(st, 1);
					tr->sq.in_place = microjxl__u(st, 1);
					tr->sq.begin_c = microjxl__u32(st, 0, 3, 8, 6, 72, 10, 1096, 13);
					tr->sq.num_c = microjxl__u32(st, 1, 0, 2, 0, 3, 0, 4, 4);
				}
				i += num_sq - 1;
				m->nb_transforms += num_sq - 1;
			}
			// H.6.2.1: apply the forward channel effects in the order the steps are
			// specified, before any channel data is decoded.
			for (j = 0; j < num_sq; ++j) {
				MICROJXL__TRY(microjxl__squeeze_forward(st, &channel, &num_channels, &nb_meta_channels, &channel_cap, &m->transform[i - num_sq + 1 + j]));
			}
			break;
		}

		default: MICROJXL__RAISE("xfm?");
		}
		MICROJXL__RAISE_DELAYED();
	}
#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[microjxl] modular: %d transforms, %d channels (%d meta)\n", m->nb_transforms, num_channels, nb_meta_channels);
	for (i = 0; i < m->nb_transforms; ++i) {
		const microjxl__transform *t = &m->transform[i];
		if (t->tr == MICROJXL__TR_SQUEEZE)
			fprintf(stderr, "[microjxl]   sq: h=%d ip=%d b=%d n=%d\n", t->sq.horizontal, t->sq.in_place, t->sq.begin_c, t->sq.num_c);
		else if (t->tr == MICROJXL__TR_PALETTE)
			fprintf(stderr, "[microjxl]   pal: b=%d n=%d nc=%d nd=%d\n", t->pal.begin_c, t->pal.num_c, t->pal.nb_colours, t->pal.nb_deltas);
		else
			fprintf(stderr, "[microjxl]   rct: b=%d t=%d\n", t->rct.begin_c, t->rct.type);
	}
	for (i = 0; i < num_channels; ++i)
		fprintf(stderr, "[microjxl]   ch%d: %dx%d hs=%d vs=%d\n", i, channel[i].width, channel[i].height, channel[i].hshift, channel[i].vshift);
#endif

	MICROJXL__SHOULD(num_channels <= st->limits->nb_channels_tr, "xlim");

	if (m->use_global_tree) {
		m->tree = global_tree;
		memcpy(&m->codespec, global_codespec, sizeof(microjxl__code_spec));
	} else {
		int32_t max_tree_size = 1024;
		for (i = 0; i < num_channels; ++i) {
			max_tree_size = microjxl__clamp_add32(max_tree_size,
				microjxl__clamp_mul32(channel[i].width, channel[i].height));
		}
		max_tree_size = microjxl__min32(1 << 20, max_tree_size);
		MICROJXL__TRY(microjxl__tree(st, max_tree_size, &m->tree, &m->codespec));
	}
	microjxl__init_code(&m->code, &m->codespec);

	m->channel = channel;
	m->num_channels = num_channels;
	m->nb_meta_channels = nb_meta_channels;
	m->dist_mult = 0;
	for (i = nb_meta_channels; i < num_channels; ++i) {
		m->dist_mult = microjxl__max32(m->dist_mult, channel[i].width);
	}
	m->dist_mult = microjxl__min32(m->dist_mult, MICROJXL__MAX_DIST_MULT);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(channel);
	microjxl__mem_free(m->transform);
	if (!m->use_global_tree) {
		microjxl__mem_free(m->tree);
		microjxl__mem_free_code_spec(&m->codespec);
	}
	m->num_channels = 0;
	m->channel = NULL;
	m->transform = NULL;
	m->tree = NULL;
	memset(&m->codespec, 0, sizeof(microjxl__code_spec));
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__allocate_modular(microjxl__st *st, microjxl__modular *m) {
	uint8_t pixel_type = (uint8_t) (st->image->modular_16bit_buffers ? MICROJXL__PLANE_I16 : MICROJXL__PLANE_I32);
	int32_t i;
	for (i = 0; i < m->num_channels; ++i) {
		microjxl__plane *c = &m->channel[i];
		if (c->width > 0 && c->height > 0) {
			int8_t hshift = c->hshift, vshift = c->vshift;
			// microjxl__init_plane resets the shifts to 0; the modular header may have
			// set them via squeeze_forward (the channel pyramid), and the pass-group
			// decode / inverse transform rely on them, so restore them here.
			MICROJXL__TRY(microjxl__init_plane(st, pixel_type, c->width, c->height, MICROJXL__PLANE_FORCE_PAD, c));
			c->hshift = hshift;
			c->vshift = vshift;
		} else { // possible when, for example, palette with only synthetic colors (nb_colours == 0)
			microjxl__init_empty_plane(c);
		}
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC void microjxl__mem_free_modular(microjxl__modular *m) {
	int32_t i;
	microjxl__mem_free_code(&m->code);
	if (!m->use_global_tree) {
		microjxl__mem_free(m->tree);
		microjxl__mem_free_code_spec(&m->codespec);
	}
	if (m->channel) {
		for (i = 0; i < m->num_channels; ++i) microjxl__mem_free_plane(&m->channel[i]);
		microjxl__mem_free(m->channel);
		m->channel = NULL;
	}
	microjxl__mem_free(m->transform);
	m->use_global_tree = 0;
	m->tree = NULL;
	memset(&m->codespec, 0, sizeof(microjxl__code_spec));
	m->transform = NULL;
	m->num_channels = 0;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// modular prediction

MICROJXL__STATIC_RETURNS_ERR microjxl__modular_channel(microjxl__st *st, microjxl__modular *m, int32_t cidx, int64_t sidx);

#ifdef MICROJXL_IMPLEMENTATION
static const int32_t MICROJXL__24DIVP1[64] = { // [i] = floor(2^24 / (i+1))
	0x1000000, 0x800000, 0x555555, 0x400000, 0x333333, 0x2aaaaa, 0x249249, 0x200000,
	0x1c71c7, 0x199999, 0x1745d1, 0x155555, 0x13b13b, 0x124924, 0x111111, 0x100000,
	0xf0f0f, 0xe38e3, 0xd7943, 0xccccc, 0xc30c3, 0xba2e8, 0xb2164, 0xaaaaa,
	0xa3d70, 0x9d89d, 0x97b42, 0x92492, 0x8d3dc, 0x88888, 0x84210, 0x80000,
	0x7c1f0, 0x78787, 0x75075, 0x71c71, 0x6eb3e, 0x6bca1, 0x69069, 0x66666,
	0x63e70, 0x61861, 0x5f417, 0x5d174, 0x5b05b, 0x590b2, 0x57262, 0x55555,
	0x53978, 0x51eb8, 0x50505, 0x4ec4e, 0x4d487, 0x4bda1, 0x4a790, 0x49249,
	0x47dc1, 0x469ee, 0x456c7, 0x44444, 0x4325c, 0x42108, 0x41041, 0x40000,
};
#endif

// ----------------------------------------
// recursion for modular buffer sizes (16/32)
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING 200
#define MICROJXL__P 16
#define MICROJXL__2P 32
#define MICROJXL__P_MIN INT16_MIN
#define MICROJXL__P_MAX INT16_MAX
#include MICROJXL_FILENAME
#undef MICROJXL__P_MIN
#undef MICROJXL__P_MAX
#define MICROJXL__P 32
#define MICROJXL__2P 64
#define MICROJXL__P_MIN INT32_MIN
#define MICROJXL__P_MAX INT32_MAX
#include MICROJXL_FILENAME
#undef MICROJXL__P_MIN
#undef MICROJXL__P_MAX
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING (-1)

#endif // MICROJXL__RECURSING < 0
#if MICROJXL__RECURSING == 200
	#define microjxl__intP MICROJXL__CONCAT3(int, MICROJXL__P, _t)
	#define microjxl__int2P MICROJXL__CONCAT3(int, MICROJXL__2P, _t)
	#define microjxl__uint2P MICROJXL__CONCAT3(uint, MICROJXL__2P, _t)
	#define MICROJXL__PIXELS MICROJXL__CONCAT3(MICROJXL__I, MICROJXL__P, _PIXELS)
// ----------------------------------------

typedef struct {
	int32_t width;
	microjxl__wp_params params;
	microjxl__int2P (*errors)[5], pred[5]; // [0..3] = sub-predictions, [4] = final prediction
	microjxl__int2P trueerrw, trueerrn, trueerrnw, trueerrne;
} microjxl__(wp,2P);

typedef struct { microjxl__intP w, n, nw, ne, nn, nee, ww, nww; } microjxl__(neighbors,P);
MICROJXL_ALWAYS_INLINE microjxl__(neighbors,P) microjxl__(init_neighbors,P)(const microjxl__plane *plane, int32_t x, int32_t y);

MICROJXL_INLINE microjxl__int2P microjxl__(gradient,2P)(microjxl__int2P w, microjxl__int2P n, microjxl__int2P nw);
MICROJXL__STATIC_RETURNS_ERR microjxl__(init_wp,2P)(microjxl__st *st, microjxl__wp_params params, int32_t width, microjxl__(wp,2P) *wp);
MICROJXL_STATIC void microjxl__(wp_before_predict_internal,2P)(
	microjxl__(wp,2P) *wp, int32_t x, int32_t y,
	microjxl__intP pw, microjxl__intP pn, microjxl__intP pnw, microjxl__intP pne, microjxl__intP pnn
);
MICROJXL_INLINE void microjxl__(wp_before_predict,2P)(microjxl__(wp,2P) *wp, int32_t x, int32_t y, microjxl__(neighbors,P) *p);
MICROJXL_INLINE microjxl__int2P microjxl__(predict,2P)(
	microjxl__st *st, int32_t pred, const microjxl__(wp,2P) *wp, const microjxl__(neighbors,P) *p
);
MICROJXL_INLINE void microjxl__(wp_after_predict,2P)(microjxl__(wp,2P) *wp, int32_t x, int32_t y, microjxl__int2P val);
MICROJXL_STATIC void microjxl__(reset_wp,2P)(microjxl__(wp,2P) *wp);
MICROJXL_STATIC void microjxl__(free_wp,2P)(microjxl__(wp,2P) *wp);
MICROJXL__STATIC_RETURNS_ERR microjxl__(modular_channel,P)(microjxl__st *st, microjxl__modular *m, int32_t cidx, int64_t sidx);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_ALWAYS_INLINE microjxl__(neighbors,P) microjxl__(init_neighbors,P)(const microjxl__plane *plane, int32_t x, int32_t y) {
	microjxl__(neighbors,P) p;
	const microjxl__intP *pixels = MICROJXL__PIXELS(plane, y);
	int32_t width = plane->width, stride = MICROJXL__PLANE_STRIDE(plane);

	/*            NN
	 *             |
	 *             v
	 * NWW  NW   _ N <- NE <- NEE
	 *  |    |   /|
	 *  v    v |/ 
	 * WW -> W  `  C
	 *
	 * A -> B means that if A doesn't exist B is used instead.
	 * if the pixel at the end of this chain doesn't exist as well, 0 is used.
	 */
	p.w = x > 0 ? pixels[x - 1] : y > 0 ? pixels[x - stride] : 0;
	p.n = y > 0 ? pixels[x - stride] : p.w;
	p.nw = x > 0 && y > 0 ? pixels[(x - 1) - stride] : p.w;
	p.ne = x + 1 < width && y > 0 ? pixels[(x + 1) - stride] : p.n;
	p.nn = y > 1 ? pixels[x - 2 * stride] : p.n;
	p.nee = x + 2 < width && y > 0 ? pixels[(x + 2) - stride] : p.ne;
	p.ww = x > 1 ? pixels[x - 2] : p.w;
	p.nww = x > 1 && y > 0 ? pixels[(x - 2) - stride] : p.ww;
	return p;
}

MICROJXL_INLINE microjxl__int2P microjxl__(gradient,2P)(microjxl__int2P w, microjxl__int2P n, microjxl__int2P nw) {
	microjxl__int2P lo = microjxl__(min,2P)(w, n), hi = microjxl__(max,2P)(w, n);
	return microjxl__(min,2P)(microjxl__(max,2P)(lo, w + n - nw), hi);
}

MICROJXL__STATIC_RETURNS_ERR microjxl__(init_wp,2P)(microjxl__st *st, microjxl__wp_params params, int32_t width, microjxl__(wp,2P) *wp) {
	typedef microjxl__int2P microjxl__i2Px5[5];
	int32_t i;
	MICROJXL__ASSERT(width > 0);
	wp->width = width;
	wp->params = params;
	MICROJXL__TRY_CALLOC(microjxl__i2Px5, &wp->errors, (size_t) width * 2);
	for (i = 0; i < 5; ++i) wp->pred[i] = 0;
	wp->trueerrw = wp->trueerrn = wp->trueerrnw = wp->trueerrne = 0;
MICROJXL__ON_ERROR:
	return st->err;
}

// also works when wp is zero-initialized (in which case does nothing)
MICROJXL_STATIC void microjxl__(wp_before_predict_internal,2P)(
	microjxl__(wp,2P) *wp, int32_t x, int32_t y,
	microjxl__intP pw, microjxl__intP pn, microjxl__intP pnw, microjxl__intP pne, microjxl__intP pnn
) {
	typedef microjxl__int2P int2P_t;
	typedef microjxl__uint2P uint2P_t;

	static const int2P_t ZERO[4] = {0, 0, 0, 0};

	int2P_t (*err)[5], (*nerr)[5];
	int2P_t w[4], wsum, sum;
	int32_t logw, i;
	const int2P_t *errw, *errn, *errnw, *errne, *errww, *errw2;

	if (!wp->errors) return;

	err = wp->errors + (y & 1 ? wp->width : 0);
	nerr = wp->errors + (y & 1 ? 0 : wp->width);

	// SPEC edge cases are handled differently from the spec, in particular some pixels are
	// added twice to err_sum and requires a special care (errw2 below)
	errw = x > 0 ? err[x - 1] : ZERO;
	errn = y > 0 ? nerr[x] : ZERO;
	errnw = x > 0 && y > 0 ? nerr[x - 1] : errn;
	errne = x + 1 < wp->width && y > 0 ? nerr[x + 1] : errn;
	errww = x > 1 ? err[x - 2] : ZERO;
	errw2 = x + 1 < wp->width ? ZERO : errw;

	// SPEC again, edge cases are handled differently
	wp->trueerrw = x > 0 ? err[x - 1][4] : 0;
	wp->trueerrn = y > 0 ? nerr[x][4] : 0;
	wp->trueerrnw = x > 0 && y > 0 ? nerr[x - 1][4] : wp->trueerrn;
	wp->trueerrne = x + 1 < wp->width && y > 0 ? nerr[x + 1][4] : wp->trueerrn;

	// (expr << 3) is used throughout wp, but it's an UB when expr is negative
	/* libjxl computes the sub-predictions and the clamping bounds in
	 * pixel_type_w (int64): the neighbor values are widened BEFORE the <<3
	 * and every add/sub/mul happens in 64 bits. The intermediate intP
	 * (int32) arithmetic below would wrap for 32-bit carriers (float bit
	 * patterns), so widen explicitly at every step. The true-error terms
	 * stay 32-bit-wrapped on purpose (see the storage below). */
	wp->pred[0] = ((int2P_t) pw + pne - pn) * 8;
	wp->pred[1] = (int2P_t) pn * 8 - (((wp->trueerrw + wp->trueerrn + wp->trueerrne) * wp->params.p1) >> 5);
	wp->pred[2] = (int2P_t) pw * 8 - (((wp->trueerrw + wp->trueerrn + wp->trueerrnw) * wp->params.p2) >> 5);
	wp->pred[3] = (int2P_t) pn * 8 - // SPEC negated (was `+`)
		((wp->trueerrnw * wp->params.p3[0] + wp->trueerrn * wp->params.p3[1] +
		  wp->trueerrne * wp->params.p3[2] + ((int2P_t) pnn - pn) * 8 * wp->params.p3[3] +
		  ((int2P_t) pnw - pw) * 8 * wp->params.p3[4]) >> 5);
	for (i = 0; i < 4; ++i) {
		/* libjxl accumulates the per-pixel errors into uint32 slots (pos_N =
		 * err(N)+err(W), pos_NW = err(NW)+err(WW), pos_NE = err(NE), each add
		 * wrapping mod 2^32) and sums the three slots as uint32 as well. The
		 * six raw terms below are that same slot structure unrolled, so the
		 * pairs must be wrapped like libjxl's slot accumulation for 32-bit
		 * data where errors can exceed 2^32. */
		uint2P_t errsum = (uint2P_t)(uint32_t)(
			  (uint32_t) errn[i]  + (uint32_t) errw[i]
			+ (uint32_t) errnw[i] + (uint32_t) errww[i]
			+ (uint32_t) errne[i] + (uint32_t) errw2[i]);
		int32_t shift = microjxl__max32(microjxl__(floor_lg,2P)(errsum + 1) - 5, 0);
		// SPEC missing the final `>> shift`
		w[i] = (int2P_t) (4 + ((int64_t) wp->params.w[i] * MICROJXL__24DIVP1[errsum >> shift] >> shift));
		#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_ETRACE") && x <= 40 && y <= 2) fprintf(stderr, "[mj-err] s=%lld x=%d y=%d i=%d errn=%u errw=%u errnw=%u errne=%u errww=%u errw2=%u errsum=%llu shift=%d\n", (long long) microjxl__debug_wp_stream_id, x, y, i, (unsigned) (uint32_t) errn[i], (unsigned) (uint32_t) errw[i], (unsigned) (uint32_t) errnw[i], (unsigned) (uint32_t) errne[i], (unsigned) (uint32_t) errww[i], (unsigned) (uint32_t) errw2[i], (unsigned long long) errsum, shift);
		#endif
	}
	#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_W")) fprintf(stderr, "[microjxl-w] x=%d y=%d w0=%d w1=%d w2=%d w3=%d logw=%d\n", x, y, (int)w[0], (int)w[1], (int)w[2], (int)w[3], (int) (microjxl__(floor_lg,2P)((uint2P_t) (w[0] + w[1] + w[2] + w[3])) - 4));
	#endif
	logw = microjxl__(floor_lg,2P)((uint2P_t) (w[0] + w[1] + w[2] + w[3])) - 4;
	wsum = sum = 0;
	for (i = 0; i < 4; ++i) {
		wsum += w[i] >>= logw;
		sum += wp->pred[i] * w[i];
	}
	// SPEC missing `- 1` before scaling
	wp->pred[4] = (int2P_t) (((int64_t) sum + (wsum >> 1) - 1) * MICROJXL__24DIVP1[wsum - 1] >> 24);
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_JWP")) {
		/* match libjxl's [jwp] trace semantics: pred4 printed pre-clamp,
		 * neighbors printed <<3 (AddBits) */
		fprintf(stderr, "[jwp] s=%lld x=%d y=%d pred0=%lld pred1=%lld pred2=%lld pred3=%lld pred4=%lld tew=%lld ten=%lld tenw=%lld tene=%lld N=%lld W=%lld NE=%lld NW=%lld NN=%lld\n",
			(long long) microjxl__debug_wp_stream_id, x, y, (long long) wp->pred[0], (long long) wp->pred[1], (long long) wp->pred[2], (long long) wp->pred[3], (long long) wp->pred[4], (long long) wp->trueerrw, (long long) wp->trueerrn, (long long) wp->trueerrnw, (long long) wp->trueerrne, (long long) pn * 8, (long long) pw * 8, (long long) pne * 8, (long long) pnw * 8, (long long) pnn * 8);
	}
#endif
	if (((wp->trueerrn ^ wp->trueerrw) | (wp->trueerrn ^ wp->trueerrnw)) <= 0) {
		int2P_t lo = microjxl__(min,2P)((int2P_t) pw, microjxl__(min,2P)((int2P_t) pn, (int2P_t) pne)) * 8; // SPEC missing shifts
		int2P_t hi = microjxl__(max,2P)((int2P_t) pw, microjxl__(max,2P)((int2P_t) pn, (int2P_t) pne)) * 8;
		wp->pred[4] = microjxl__(min,2P)(microjxl__(max,2P)(lo, wp->pred[4]), hi);
	}
}

MICROJXL_INLINE void microjxl__(wp_before_predict,2P)(
	microjxl__(wp,2P) *wp, int32_t x, int32_t y, microjxl__(neighbors,P) *p
) {
	microjxl__(wp_before_predict_internal,2P)(wp, x, y, p->w, p->n, p->nw, p->ne, p->nn);
}

MICROJXL_INLINE microjxl__int2P microjxl__(predict,2P)(
	microjxl__st *st, int32_t pred, const microjxl__(wp,2P) *wp, const microjxl__(neighbors,P) *p
) {
	typedef microjxl__int2P i2;
	/* libjxl evaluates every predictor in pixel_type_w (int64). With intP =
	 * int32 planes (float bit patterns) the intermediate sums exceed INT32
	 * and must not wrap, so widen every operand before the arithmetic. */
	switch (pred) {
	case 0: return 0;
	case 1: return p->w;
	case 2: return p->n;
	case 3: return (i2) ((i2) p->w + p->n) / 2; /* SPEC: C division truncates toward zero (libjxl (left+top)/2); >> 1 floors and is wrong for negative odd sums */
	case 4: return microjxl__(abs,2P)((i2) p->n - p->nw) < microjxl__(abs,2P)((i2) p->w - p->nw) ? p->w : p->n;
	case 5: return microjxl__(gradient,2P)(p->w, p->n, p->nw);
	case 6: return (wp->pred[4] + 3) >> 3;
	case 7: return p->ne;
	case 8: return p->nw;
	case 9: return p->ww;
	case 10: return (i2) ((i2) p->w + p->nw) / 2; /* trunc, not >>1 */
	case 11: return (i2) ((i2) p->n + p->nw) / 2; /* trunc, not >>1 */
	case 12: return (i2) ((i2) p->n + p->ne) / 2; /* trunc, not >>1 */
	case 13: return ((i2) 6 * p->n - (i2) 2 * p->nn + (i2) 7 * p->w + p->ww + p->nee + (i2) 3 * p->ne + 8) / 16;
	default: return MICROJXL__ERR("pred"), 0;
	}
}

// also works when wp is zero-initialized (in which case does nothing)
MICROJXL_INLINE void microjxl__(wp_after_predict,2P)(microjxl__(wp,2P) *wp, int32_t x, int32_t y, microjxl__int2P val) {
	if (wp->errors) {
		microjxl__int2P *err = wp->errors[(y & 1 ? wp->width : 0) + x];
		int32_t i;
		// SPEC approximated differently from the spec
		/* libjxl stores the sub-prediction errors in std::vector<uint32_t> and
		 * the true error in std::vector<int32_t>: both wrap to 32 bits on
		 * store. 32-bit float bit patterns produce errors beyond +-2^31, and
		 * property 15 (max WP error) reads these values back, so the wrap is
		 * observable and must be reproduced. */
		for (i = 0; i < 4; ++i) {
			err[i] = (microjxl__int2P)(uint32_t)((microjxl__(abs,2P)(wp->pred[i] - val * 8) + 3) >> 3);
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_JETRACE") && x <= 40 && y <= 2) fprintf(stderr, "[je] s=%lld x=%d y=%d i=%d err=%u pred=%lld val=%lld\n", (long long) microjxl__debug_wp_stream_id, x, y, i, (unsigned) (uint32_t) err[i], (long long) wp->pred[i], (long long) (val * 8));
#endif
		}
		err[4] = (microjxl__int2P)(int32_t)(wp->pred[4] - val * 8); // SPEC this is a *signed* difference
	}
}

// also works when wp is zero-initialized (in which case does nothing)
MICROJXL_STATIC void microjxl__(reset_wp,2P)(microjxl__(wp,2P) *wp) {
	int32_t i;
	if (wp->errors) memset(wp->errors, 0, (size_t) wp->width * 2 * sizeof(microjxl__int2P[5]));
	for (i = 0; i < 5; ++i) wp->pred[i] = 0;
	wp->trueerrw = wp->trueerrn = wp->trueerrnw = wp->trueerrne = 0;
}

MICROJXL_STATIC void microjxl__(free_wp,2P)(microjxl__(wp,2P) *wp) {
	microjxl__mem_free(wp->errors);
	wp->errors = NULL;
	wp->width = 0;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__(modular_channel,P)(
	microjxl__st *st, microjxl__modular *m, int32_t cidx, int64_t sidx
) {
	typedef microjxl__intP intP_t;
	typedef microjxl__int2P int2P_t;

	microjxl__plane *c = &m->channel[cidx];
	int32_t width = c->width, height = c->height;
	int32_t y, x, i;
	int32_t nrefcmap, *refcmap = NULL; // refcmap[i] is a channel index for properties (16..19)+4*i
	microjxl__(wp,2P) wp = MICROJXL__INIT;

	MICROJXL__ASSERT(m->tree); // caller should set this to the global tree if not given
	MICROJXL__ASSERT(c->type == MICROJXL__(PLANE_I,P));

	{ // determine whether to use weighted predictor (expensive)
		int32_t lasttree = 0, use_wp = 0;
		for (i = 0; i <= lasttree && !use_wp; ++i) {
			if (m->tree[i].branch.prop < 0) {
				use_wp |= ~m->tree[i].branch.prop == 15;
				lasttree = microjxl__max32(lasttree,
					i + microjxl__max32(m->tree[i].branch.leftoff, m->tree[i].branch.rightoff));
			} else {
				use_wp |= m->tree[i].leaf.predictor == 6;
			}
		}
		if (use_wp) MICROJXL__TRY(microjxl__(init_wp,2P)(st, m->wp, width, &wp));
	}
#ifdef MICROJXL_DEBUG
	microjxl__debug_wp_stream_id = sidx;
#endif

	// compute indices for additional "previous channel" properties
	// SPEC incompatible channels are skipped and never result in unusable but numbered properties
	MICROJXL__TRY_MALLOC(int32_t, &refcmap, (size_t) cidx);
	nrefcmap = 0;
	for (i = cidx - 1; i >= 0; --i) {
		microjxl__plane *refc = &m->channel[i];
		if (c->width != refc->width || c->height != refc->height) continue;
		if (c->hshift != refc->hshift || c->vshift != refc->vshift) continue;
		refcmap[nrefcmap++] = i;
	}

	for (y = 0; y < height; ++y) {
		intP_t *outpixels = MICROJXL__PIXELS(c, y);
		int2P_t prop9_prev = 0; // SPEC H.4: property 8 uses property 9 at (x-1, y)
		for (x = 0; x < width; ++x) {
			microjxl__tree_node *n = m->tree;
			microjxl__(neighbors,P) p = microjxl__(init_neighbors,P)(c, x, y);
			int2P_t val;

			// wp should be calculated before any property testing due to max_error (property 15)
			microjxl__(wp_before_predict,2P)(&wp, x, y, &p);
			#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_NEIGH")) fprintf(stderr, "[microjxl-neigh] c=%d x=%d y=%d w=%d n=%d nw=%d ne=%d nn=%d ww=%d\n", cidx, x, y, (int) p.w, (int) p.n, (int) p.nw, (int) p.ne, (int) p.nn, (int) p.ww);
			#endif

			while (n->branch.prop < 0) {
				int32_t refcidx;
				microjxl__plane *refc;

				switch (~n->branch.prop) {
				case 0: val = cidx; break;
				case 1: val = (int2P_t) sidx; break; // TODO check overflow
				case 2: val = y; break;
				case 3: val = x; break;
				case 4: val = microjxl__(abs,2P)(p.n); break;
				case 5: val = microjxl__(abs,2P)(p.w); break;
				case 6: val = p.n; break;
				case 7: val = p.w; break;
				// SPEC H.4 Table H.4: property 8 is W minus property 9 at (x-1, y),
				// NOT W - (WW + NW - NWW) (the latter only matches for x > 1).
				// libjxl keeps p[9] = left+top-topleft from the previous pixel and
				// computes p[8] = left - p[9] before overwriting it, with p[9]
				// reset to 0 at the start of each row (InitPropsRow).
				// libjxl computes each property expression in pixel_type_w
				// (int64; the neighbors are widened BEFORE the arithmetic, so
				// the sums/differences never overflow) and then stores it into
				// PropertyVal (int32, options.h) regardless of the buffer
				// width; the tree compares that int32 value. Mirror exactly:
				// int64 arithmetic + int32 wrap store (defined, no UB).
				case 8: val = x > 0 ? (int32_t) (uint32_t) ((int2P_t) p.w - prop9_prev) : p.w; break;
				case 9: val = (int32_t) (uint32_t) ((int2P_t) p.w + p.n - p.nw); break;
				case 10: val = (int32_t) (uint32_t) ((int2P_t) p.w - p.nw); break;
				case 11: val = (int32_t) (uint32_t) ((int2P_t) p.nw - p.n); break;
				case 12: val = (int32_t) (uint32_t) ((int2P_t) p.n - p.ne); break;
				case 13: val = (int32_t) (uint32_t) ((int2P_t) p.n - p.nn); break;
				case 14: val = (int32_t) (uint32_t) ((int2P_t) p.w - p.ww); break;
				case 15: // requires use_wp; otherwise will be 0
					val = wp.trueerrw;
					if (microjxl__(abs,2P)(val) < microjxl__(abs,2P)(wp.trueerrn)) val = wp.trueerrn;
					if (microjxl__(abs,2P)(val) < microjxl__(abs,2P)(wp.trueerrnw)) val = wp.trueerrnw;
					if (microjxl__(abs,2P)(val) < microjxl__(abs,2P)(wp.trueerrne)) val = wp.trueerrne;
					break;
				default:
					refcidx = (~n->branch.prop - 16) / 4;
					if (refcidx >= nrefcmap) {
						/* libjxl sizes the reference buffer from the tree's maximum
						 * property (rounded up to whole channels) and zero-fills it, so
						 * a tree may reference more previous channels than exist as
						 * compatible neighbors; those properties read as zero. */
						val = 0;
						break;
					}
					refc = &m->channel[refcmap[refcidx]];
					MICROJXL__ASSERT(c->width == refc->width && c->height == refc->height);
					val = MICROJXL__PIXELS(refc, y)[x]; // rC
					if (~n->branch.prop & 2) {
						int2P_t rw = x > 0 ? MICROJXL__PIXELS(refc, y)[x - 1] : 0;
						int2P_t rn = y > 0 ? MICROJXL__PIXELS(refc, y - 1)[x] : rw;
						int2P_t rnw = x > 0 && y > 0 ? MICROJXL__PIXELS(refc, y - 1)[x - 1] : rw;
						val -= microjxl__(gradient,2P)(rw, rn, rnw);
					}
					// spec H.4.1 Table H.4: the four props per reference channel are
					// |rC|, rC, |rC - rG|, rC - rG -- abs applies on the even offsets (0, 2)
					if (!(~n->branch.prop & 1)) val = microjxl__(abs,2P)(val);
					break;
				}
				#ifdef MICROJXL_DEBUG
				if (getenv("MICROJXL_TRACE_PROPS")) fprintf(stderr, "[microjxl-props] c=%d x=%d y=%d node=%d prop=%d val=%lld split=%d left=%d\n", cidx, x, y, (int) (n - m->tree), ~n->branch.prop, (long long) val, (int) n->branch.value, (int) (val > n->branch.value));
				if (getenv("MICROJXL_PALL") && ((cidx == 0 && y >= 124 && y <= 126 && x < 2) || (cidx == 0 && y >= 249 && y <= 251 && x < 2) || (cidx <= 2 && y < 2 && x < 2) || (cidx == 1 && y >= 1 && y <= 3 && x >= 166 && x <= 170) || (cidx == 0 && y == 1 && x >= 160 && x <= 172))) fprintf(stderr, "[mjprops] c=%d x=%d y=%d node=%d prop=%d val=%lld split=%d left=%d\n", cidx, x, y, (int) (n - m->tree), ~n->branch.prop, (long long) val, (int) n->branch.value, (int) (val > n->branch.value));
				#endif
				n += val > n->branch.value ? n->branch.leftoff : n->branch.rightoff;
			}

			val = microjxl__code(st, n->leaf.ctx, m->dist_mult, &m->code);
			#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_MPROPS") && cidx == 0 && y < 2 && x < 3) fprintf(stderr, "[mprops] c=%d x=%d y=%d ctx=%d guess=%d mult=%d props: %d %d %d %d %d %d %d %d %d\n", cidx, x, y, (m->codespec.cluster_map ? m->codespec.cluster_map[n->leaf.ctx] : n->leaf.ctx), (int) microjxl__(predict,2P)(st, n->leaf.predictor, &wp, &p), n->leaf.multiplier, cidx, (int) sidx & 0xffff, y, (int) x, (int) microjxl__(abs,2P)(p.n), (int) microjxl__(abs,2P)(p.w), (int) p.n, (int) p.w, (int) (p.w - (x > 0 ? p.w + p.n - p.nw : 0)));
			if (getenv("MICROJXL_TRACE_TOKEN")) fprintf(stderr, "[microjxl-tk] c=%d x=%d y=%d raw=%d\n", cidx, x, y, (int) val);
			if (getenv("MICROJXL_TRACE_CODE")) fprintf(stderr, "[microjxl-code] c=%d x=%d y=%d ans_state=%u\n", cidx, x, y, (unsigned) m->code.ans_state);
			#endif
			// TODO can overflow at any operator and the bound is incorrect anyway
			#ifdef MICROJXL_DEBUG
			int32_t raw_code_val = val;
			#endif
			/* libjxl's make_pixel computes unpack_signed(v) * multiplier + offset
			 * in pixel_type_w (int64) and adds the guess in int64; the int32
			 * intermediate wrapped for 32-bit carriers (float bit patterns). */
			val = (microjxl__int2P) microjxl__unpack_signed((int32_t) val) * n->leaf.multiplier + n->leaf.offset;
			#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_MVAL") && ((cidx == 0 && y >= 124 && y <= 126 && x < 4) || (cidx == 0 && y >= 249 && y <= 251 && x < 4) || (cidx == 1 && y >= 1 && y <= 3 && x >= 166 && x <= 172) || (cidx == 6 && y == 126 && x >= 240 && x <= 246))) {
				fprintf(stderr, "[mjval] c=%d y=%d x=%d ctx=%d ans=%u raw=%d ret=%d mult=%d off=%d guess=%d pixel=%d\n", cidx, y, x, n->leaf.ctx, (unsigned) m->code.ans_state, raw_code_val, (int) microjxl__unpack_signed(raw_code_val), n->leaf.multiplier, n->leaf.offset, (int) microjxl__(predict,2P)(st, n->leaf.predictor, &wp, &p), (int) val);
			}
			if (getenv("MICROJXL_MVAL") && cidx <= 1 && y < 2 && x < 176) {
				fprintf(stderr, "[mjval1] c=%d s=%lld y=%d x=%d ctx=%d raw=%d ret=%d mult=%d off=%d guess=%d pixel=%d\n", cidx, (long long) sidx, y, x, n->leaf.ctx, raw_code_val, (int) microjxl__unpack_signed(raw_code_val), n->leaf.multiplier, n->leaf.offset, (int) microjxl__(predict,2P)(st, n->leaf.predictor, &wp, &p), (int) val);
			}
			if (getenv("MICROJXL_TRACE_PRED")) fprintf(stderr, "[microjxl-pred] c=%d x=%d y=%d guess=%d mult=%d off=%d\n", cidx, x, y, (int) microjxl__(predict,2P)(st, n->leaf.predictor, &wp, &p), n->leaf.multiplier, n->leaf.offset);
			#endif
			val += microjxl__(predict,2P)(st, n->leaf.predictor, &wp, &p);
			/* The decoded sample must fit the modular plane's own
			 * range: INT16 for 16-bit buffers (P=16), INT32 for
			 * 32-bit buffers (P=32). The old hardcoded INT16 check
			 * rejected every lossless/high-depth image (fm32 gate
			 * blocked them earlier; without it the 32-bit recursion
			 * still failed here). */
			#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_TREEVAL")) fprintf(stderr, "[microjxl-tv] c=%d x=%d y=%d ctx=%d pred=%d val=%lld\n", cidx, x, y, n->leaf.ctx, n->leaf.predictor, (long long) val);
			#endif
			/* No range check here: libjxl stores the decoded sample into the
			 * plane with implicit wrap (e.g. 32-bit float bit patterns can
			 * exceed INT32_MAX when interpreted as the int64 accumulator).
			 * Raising here rejected valid float-modular files. */
			outpixels[x] = (intP_t) val;
			#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_TREEVAL")) fprintf(stderr, "[microjxl-tv] c=%d x=%d y=%d ctx=%d pred=%d val=%d\n", cidx, x, y, n->leaf.ctx, n->leaf.predictor, (int) val);
			if (getenv("MICROJXL_TRACE_WP")) fprintf(stderr, "[jwp] c=%d x=%d y=%d pred0=%d pred1=%d pred2=%d pred3=%d pred4=%d tew=%d ten=%d tenw=%d tene=%d\n", cidx, x, y, (int)wp.pred[0], (int)wp.pred[1], (int)wp.pred[2], (int)wp.pred[3], (int)wp.pred[4], (int)wp.trueerrw, (int)wp.trueerrn, (int)wp.trueerrnw, (int)wp.trueerrne);
			#endif
			/* libjxl's make_pixel returns pixel_type (int32): the residual sum is
			 * truncated to 32 bits BEFORE the WP error update (UpdateErrors sees
			 * the wrapped pixel, and property 15's trueerr derives from it). */
			microjxl__(wp_after_predict,2P)(&wp, x, y, (microjxl__int2P)(int32_t) val);
			// SPEC H.4: property 9 for the next pixel (x+1)'s property 8.
			// libjxl stores this in p[9] (PropertyVal = int32) after computing
			// p[8] = left - p[9]; keep the int32-width stored value here.
			prop9_prev = (int32_t) (uint32_t) ((int2P_t) p.w + p.n - p.nw);
		}
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_MDUMP")) {
			char fn[256];
			snprintf(fn, sizeof(fn), "/tmp/mj_plane_c%d_s%lld.bin", cidx, (long long) sidx);
			FILE *f = fopen(fn, "wb");
			if (f) {								int32_t yy;
								for (yy = 0; yy < height; ++yy)
									fwrite(MICROJXL__PIXELS(c, yy), sizeof(intP_t), (size_t) width, f);
				fclose(f);
			}
		}
#endif
	}

	microjxl__(free_wp,2P)(&wp);
	microjxl__mem_free(refcmap);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__(free_wp,2P)(&wp);
	microjxl__mem_free(refcmap);
	microjxl__mem_free_plane(c);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

// ----------------------------------------
// end of recursion
	#undef microjxl__intP
	#undef microjxl__int2P
	#undef microjxl__uint2P
	#undef MICROJXL__PIXELS
	#undef MICROJXL__P
	#undef MICROJXL__2P
#endif // MICROJXL__RECURSING == 200
#if MICROJXL__RECURSING < 0
// ----------------------------------------

#ifdef MICROJXL_IMPLEMENTATION
MICROJXL__STATIC_RETURNS_ERR microjxl__modular_channel(microjxl__st *st, microjxl__modular *m, int32_t cidx, int64_t sidx) {
	switch (m->channel[cidx].type) {
		case MICROJXL__PLANE_I16: return microjxl__modular_channel16(st, m, cidx, sidx);
		case MICROJXL__PLANE_I32: return microjxl__modular_channel32(st, m, cidx, sidx);
		case MICROJXL__PLANE_EMPTY: return 0;
		default: MICROJXL__UNREACHABLE(); return 0;
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////
// modular (inverse) transform

MICROJXL__STATIC_RETURNS_ERR microjxl__inverse_transform(microjxl__st *st, microjxl__modular *m);

#ifdef MICROJXL_IMPLEMENTATION
#define MICROJXL__X(x,y,z) {x,y,z}, {-(x),-(y),-(z)}
#define MICROJXL__XX(a,b,c,d,e,f) MICROJXL__X a, MICROJXL__X b, MICROJXL__X c, MICROJXL__X d, MICROJXL__X e, MICROJXL__X f
static const int16_t MICROJXL__PALETTE_DELTAS[144][3] = { // the first entry is a duplicate and skipped
	MICROJXL__XX((0, 0, 0), (4, 4, 4), (11, 0, 0), (0, 0, -13), (0, -12, 0), (-10, -10, -10)),
	MICROJXL__XX((-18, -18, -18), (-27, -27, -27), (-18, -18, 0), (0, 0, -32), (-32, 0, 0), (-37, -37, -37)),
	MICROJXL__XX((0, -32, -32), (24, 24, 45), (50, 50, 50), (-45, -24, -24), (-24, -45, -45), (0, -24, -24)),
	MICROJXL__XX((-34, -34, 0), (-24, 0, -24), (-45, -45, -24), (64, 64, 64), (-32, 0, -32), (0, -32, 0)),
	MICROJXL__XX((-32, 0, 32), (-24, -45, -24), (45, 24, 45), (24, -24, -45), (-45, -24, 24), (80, 80, 80)),
	MICROJXL__XX((64, 0, 0), (0, 0, -64), (0, -64, -64), (-24, -24, 45), (96, 96, 96), (64, 64, 0)),
	MICROJXL__XX((45, -24, -24), (34, -34, 0), (112, 112, 112), (24, -45, -45), (45, 45, -24), (0, -32, 32)),
	MICROJXL__XX((24, -24, 45), (0, 96, 96), (45, -24, 24), (24, -45, -24), (-24, -45, 24), (0, -64, 0)),
	MICROJXL__XX((96, 0, 0), (128, 128, 128), (64, 0, 64), (144, 144, 144), (96, 96, 0), (-36, -36, 36)),
	MICROJXL__XX((45, -24, -45), (45, -45, -24), (0, 0, -96), (0, 128, 128), (0, 96, 0), (45, 24, -45)),
	MICROJXL__XX((-128, 0, 0), (24, -45, 24), (-45, 24, -45), (64, 0, -64), (64, -64, -64), (96, 0, 96)),
	MICROJXL__XX((45, -45, 24), (24, 45, -45), (64, 64, -64), (128, 128, 0), (0, 0, -128), (-24, 45, -45)),
};
#undef MICROJXL__X
#undef MICROJXL__XX
#endif // defined MICROJXL_IMPLEMENTATION

// ----------------------------------------
// recursion for modular inverse transform
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING 300
#define MICROJXL__P 16
#define MICROJXL__2P 32
#include MICROJXL_FILENAME
#define MICROJXL__P 32
#define MICROJXL__2P 64
#include MICROJXL_FILENAME
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING (-1)

#endif // MICROJXL__RECURSING < 0
#if MICROJXL__RECURSING == 300
	#define microjxl__intP MICROJXL__CONCAT3(int, MICROJXL__P, _t)
	#define microjxl__int2P MICROJXL__CONCAT3(int, MICROJXL__2P, _t)
	#define MICROJXL__PIXELS MICROJXL__CONCAT3(MICROJXL__I, MICROJXL__P, _PIXELS)
// ----------------------------------------

MICROJXL_STATIC void microjxl__(inverse_rct,P)(microjxl__modular *m, const microjxl__transform *tr);
MICROJXL__STATIC_RETURNS_ERR microjxl__(inverse_palette,P)(microjxl__st *st, microjxl__modular *m, const microjxl__transform *tr);
MICROJXL__STATIC_RETURNS_ERR microjxl__(inverse_squeeze,P)(microjxl__st *st, microjxl__modular *m, const microjxl__transform *tr);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_STATIC void microjxl__(inverse_rct,P)(microjxl__modular *m, const microjxl__transform *tr) {
	typedef microjxl__intP intP_t;
	typedef microjxl__int2P int2P_t;

	// SPEC permutation psuedocode is missing parentheses; better done with a LUT anyway
	static const uint8_t PERMUTATIONS[6][3] = {{0,1,2},{1,2,0},{2,0,1},{0,2,1},{1,0,2},{2,1,0}};

	microjxl__plane c[3];
	int32_t x, y, i;

	MICROJXL__ASSERT(tr->tr == MICROJXL__TR_RCT);
	for (i = 0; i < 3; ++i) c[i] = m->channel[tr->rct.begin_c + i];
	MICROJXL__ASSERT(microjxl__plane_all_equal_sized(c, c + 3));

	// it is possible that input planes are empty, in which case we do nothing (not even shuffling)
	if (c->type == MICROJXL__PLANE_EMPTY) {
		MICROJXL__ASSERT(microjxl__plane_all_equal_typed(c, c + 3) == MICROJXL__PLANE_EMPTY);
		return;
	} else {
		MICROJXL__ASSERT(microjxl__plane_all_equal_typed(c, c + 3) == MICROJXL__(PLANE_I,P));
	}

	// TODO detect overflow
	/* libjxl's InvRCTRow does all its arithmetic in pixel_type (int32)
	 * with PixelAdd = (int32)(uint32)(a + b), i.e. the additions WRAP
	 * modulo 2^32 (SIMD integer adds; transform.h PixelAdd), and shifts
	 * happen on the wrapped int32 value; only the final store truncates
	 * to the container. Mutated streams can hit that wrap, so mirror it
	 * with a defined uint32 sum (same bit pattern as libjxl) instead of
	 * relying on C signed-add UB. */
	#define MICROJXL__RCT_ADD(a, b) ((int32_t) (uint32_t) ((uint32_t) (a) + (uint32_t) (b)))
	switch (tr->rct.type % 7) {
	case 0: break;
	case 1:
		for (y = 0; y < c->height; ++y) {
			intP_t *pp0 = MICROJXL__PIXELS(&c[0], y), *pp2 = MICROJXL__PIXELS(&c[2], y);
			for (x = 0; x < c->width; ++x) pp2[x] = MICROJXL__RCT_ADD(pp2[x], pp0[x]);
		}
		break;
	case 2:
		/* libjxl InvRCTRow<2>: second = custom >> 1 == 1 -> Second += First
		 * (Third untouched). Undo of the forward "Second -= First". */
		for (y = 0; y < c->height; ++y) {
			intP_t *pp0 = MICROJXL__PIXELS(&c[0], y), *pp1 = MICROJXL__PIXELS(&c[1], y);
			for (x = 0; x < c->width; ++x) pp1[x] = MICROJXL__RCT_ADD(pp1[x], pp0[x]);
		}
		break;
	case 3:
		for (y = 0; y < c->height; ++y) {
			intP_t *pp0 = MICROJXL__PIXELS(&c[0], y), *pp1 = MICROJXL__PIXELS(&c[1], y), *pp2 = MICROJXL__PIXELS(&c[2], y);
			for (x = 0; x < c->width; ++x) {
				pp1[x] = MICROJXL__RCT_ADD(pp1[x], pp0[x]);
				pp2[x] = MICROJXL__RCT_ADD(pp2[x], pp0[x]);
			}
		}
		break;
	case 4:
		/* libjxl InvRCTRow<4>: Second = PixelAdd(Second, PixelAdd(First, Third) >> 1) */
		for (y = 0; y < c->height; ++y) {
			intP_t *pp0 = MICROJXL__PIXELS(&c[0], y), *pp1 = MICROJXL__PIXELS(&c[1], y), *pp2 = MICROJXL__PIXELS(&c[2], y);
			for (x = 0; x < c->width; ++x) pp1[x] = (intP_t) MICROJXL__RCT_ADD(pp1[x], MICROJXL__RCT_ADD(pp0[x], pp2[x]) >> 1);
		}
		break;
	case 5:
		/* libjxl InvRCTRow<5>: Third = PixelAdd(Third, First) FIRST, then
		 * Second = PixelAdd(Second, PixelAdd(First, Third) >> 1) using the
		 * UPDATED Third; mirror that op-for-op (the intermediate wrapped
		 * Third is kept at int32 width for Second, only the container
		 * store truncates). */
		for (y = 0; y < c->height; ++y) {
			intP_t *pp0 = MICROJXL__PIXELS(&c[0], y), *pp1 = MICROJXL__PIXELS(&c[1], y), *pp2 = MICROJXL__PIXELS(&c[2], y);
			for (x = 0; x < c->width; ++x) {
				int32_t third = MICROJXL__RCT_ADD(pp2[x], pp0[x]);
				pp2[x] = (intP_t) third;
				pp1[x] = (intP_t) MICROJXL__RCT_ADD(pp1[x], MICROJXL__RCT_ADD(pp0[x], third) >> 1);
			}
		}
		break;
	case 6: // YCgCo
		/* libjxl InvRCTRow<6>: tmp = PixelAdd(Y, -(Cg >> 1)); G = PixelAdd(Cg, tmp);
		 * B = PixelAdd(tmp, -(Co >> 1)); R = PixelAdd(B, Co) — every add wraps. */
		for (y = 0; y < c->height; ++y) {
			intP_t *pp0 = MICROJXL__PIXELS(&c[0], y), *pp1 = MICROJXL__PIXELS(&c[1], y), *pp2 = MICROJXL__PIXELS(&c[2], y);
			for (x = 0; x < c->width; ++x) {
				int32_t tmp = MICROJXL__RCT_ADD(pp0[x], -(pp2[x] >> 1));
				int32_t p1 = MICROJXL__RCT_ADD(pp2[x], tmp);
				int32_t p2 = MICROJXL__RCT_ADD(tmp, -(pp1[x] >> 1));
				pp0[x] = (intP_t) MICROJXL__RCT_ADD(p2, pp1[x]);
				pp1[x] = (intP_t) p1;
				pp2[x] = (intP_t) p2;
			}
		}
		break;
	default: MICROJXL__UNREACHABLE();
	}
	#undef MICROJXL__RCT_ADD

	for (i = 0; i < 3; ++i) {
		m->channel[tr->rct.begin_c + PERMUTATIONS[tr->rct.type / 7][i]] = c[i];
	}
}

MICROJXL__STATIC_RETURNS_ERR microjxl__(inverse_palette,P)(
	microjxl__st *st, microjxl__modular *m, const microjxl__transform *tr
) {
	typedef microjxl__intP intP_t;
	typedef microjxl__int2P int2P_t;

	// `first` is the index channel index; restored color channels will be at indices [first,last],
	// where the original index channel is relocated to the index `last` and then repurposed.
	// the palette meta channel 0 will be removed at the very end.
	int32_t first = tr->pal.begin_c + 1, last = tr->pal.begin_c + tr->pal.num_c, bpp = st->image->bpp;
	int32_t i, j, y, x;
	microjxl__plane *idxc;
	int32_t width = m->channel[first].width, height = m->channel[first].height;
	int use_pred = !(tr->pal.nb_deltas == 0 && tr->pal.d_pred == 0), use_wp = use_pred && tr->pal.d_pred == 6;
	/* libjxl InvPalette: the predictor path is taken whenever nb_deltas > 0 OR
	 * the delta predictor is not Zero; negative indices then mean "delta entry,
	 * value = prediction + palette/delta-table entry". Only the
	 * nb_deltas == 0 && Zero case decodes without a predictor (single
	 * paletted channel: index clamped into the table; multiple channels:
	 * plain GetPaletteValue per channel). */
	microjxl__(wp,2P) wp = MICROJXL__INIT;

	MICROJXL__ASSERT(tr->tr == MICROJXL__TR_PALETTE);

	// since we never shrink m->channel, we know there is enough capacity for intermediate transform
	memmove(m->channel + last, m->channel + first, sizeof(microjxl__plane) * (size_t) (m->num_channels - first));
	m->num_channels += last - first;
	idxc = &m->channel[last];

	for (i = first; i < last; ++i) m->channel[i].type = 0;
	if (idxc->type == MICROJXL__PLANE_EMPTY) {
		// index channel is empty; all resulting output channels would be empty
		for (i = first; i < last; ++i) microjxl__init_empty_plane(&m->channel[i]);
	} else {
		for (i = first; i < last; ++i) {
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__(PLANE_I,P), width, height, 0, &m->channel[i]));
		}
	}

	if (use_wp) MICROJXL__TRY(microjxl__(init_wp,2P)(st, m->wp, width, &wp));

	for (i = 0; i < tr->pal.num_c; ++i) {
		// palette channel can be also empty
		intP_t *palp = tr->pal.nb_colours > 0 ? MICROJXL__PIXELS(&m->channel[0], i) : NULL;
		microjxl__plane *c = &m->channel[first + i];
		for (y = 0; y < height; ++y) {
			// SPEC pseudocode accidentally overwrites the index channel
			intP_t *idxline = MICROJXL__PIXELS(idxc, y);
			intP_t *line = MICROJXL__PIXELS(c, y);
			for (x = 0; x < width; ++x) {
				intP_t idx = idxline[x], val;
				int is_delta = idx < tr->pal.nb_deltas;
				if (tr->pal.num_c == 1 && tr->pal.nb_deltas == 0 && tr->pal.d_pred == 0) {
					/* libjxl InvPalette "UndoChannelPalette" path: with a single
					 * paletted channel, zero deltas and the Zero predictor, the
					 * index is clamped into the table and looked up directly; the
					 * hard-coded delta entries and the synthesized RGB cubes are
					 * unreachable in this case. */
					if (tr->pal.nb_colours <= 0) val = 0;
					else {
						idx = idx < 0 ? 0 : (idx >= tr->pal.nb_colours ? (intP_t) (tr->pal.nb_colours - 1) : idx);
						val = palp[idx];
					}
				} else if (idx < 0) { // hard-coded delta for first 3 channels, otherwise 0
					if (i < 3) {
						idx = (intP_t) (~idx % 143); // say no to 1's complement
						val = MICROJXL__PALETTE_DELTAS[idx + 1][i];
						if (bpp > 8) val = (intP_t) (val << (microjxl__min32(bpp, 24) - 8));
					} else {
						val = 0;
					}
				} else if (idx < tr->pal.nb_colours) {
					val = palp[idx];					} else { // synthesized from (idx - nb_colours); 0 for non-RGB channels (libjxl)
						idx = (intP_t) (idx - tr->pal.nb_colours);
						if (i >= 3) val = 0;
						else if (idx < 64) { // idx == ..YX in base 4 -> {(X+0.5)/4, (Y+0.5)/4, ...}
							val = (intP_t) (((idx >> (2 * i)) & 3) * (((int2P_t) 1 << bpp) - 1) / 4 +
								((int2P_t) 1 << microjxl__max32(0, bpp - 3)));
						} else { // idx + 64 == ..ZYX in base 5 -> {X/4, Y/4, Z/4, ...}
							val = (intP_t) (idx - 64);
							for (j = 0; j < i; ++j) val = (intP_t) (val / 5);
							val = (intP_t) ((val % 5) * ((1 << bpp) - 1) / 4);
						}
					}
				if (use_pred) {
					microjxl__(neighbors,P) p = microjxl__(init_neighbors,P)(c, x, y);
					microjxl__(wp_before_predict,2P)(&wp, x, y, &p);
					// TODO handle overflow
					if (is_delta) val = (intP_t) (val + microjxl__(predict,2P)(st, tr->pal.d_pred, &wp, &p));
					microjxl__(wp_after_predict,2P)(&wp, x, y, val);
				}
				line[x] = val;
			}
		}
		microjxl__(reset_wp,2P)(&wp);
	}

	microjxl__(free_wp,2P)(&wp);
	microjxl__mem_free_plane(&m->channel[0]);
	memmove(m->channel, m->channel + 1, sizeof(microjxl__plane) * (size_t) --m->num_channels);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__(free_wp,2P)(&wp);
	return st->err;
}

// H.6.2.2/3: the tendency function, used by the inverse squeeze steps.
// Note that Idiv rounds towards zero, which is what plain / does in C.
MICROJXL_STATIC microjxl__int2P microjxl__(tendency,P)(microjxl__int2P A, microjxl__int2P B, microjxl__int2P C) {
	microjxl__int2P X;
	if (A >= B && B >= C) {
		X = (4 * A - 3 * C - B + 6) / 12;
		if (X - (X & 1) > 2 * (A - B)) X = 2 * (A - B) + 1;
		if (X + (X & 1) > 2 * (B - C)) X = 2 * (B - C);
		return X;
	} else if (A <= B && B <= C) {
		X = (4 * A - 3 * C - B - 6) / 12;
		if (X + (X & 1) < 2 * (A - B)) X = 2 * (A - B) - 1;
		if (X - (X & 1) < 2 * (B - C)) X = 2 * (B - C);
		return X;
	}
	return 0;
}

// H.6.2.2/3: inverse squeeze step. The channel `c` holds the half-size "avg" data
// and the residual channel at index `r` holds the diff data; they are merged back
// into the full-size channel. The residual is then removed from the channel list.
// The residual position is fixed for the whole step: right after the squeezed
// channels (in_place) or at the tail of the channel list (otherwise); every
// removal shifts the next residual into the same index.
MICROJXL__STATIC_RETURNS_ERR microjxl__(inverse_squeeze,P)(
	microjxl__st *st, microjxl__modular *m, const microjxl__transform *tr
) {
	typedef microjxl__intP intP_t;
	typedef microjxl__int2P int2P_t;

	int32_t begin_c = tr->sq.begin_c, num_c = tr->sq.num_c, end_c = begin_c + num_c - 1;
	// spec: r = in_place ? end + 1 : channel.size() + begin - end - 1, once per step
	int32_t r = tr->sq.in_place ? end_c + 1 : m->num_channels + begin_c - end_c - 1;
	int32_t c, x, y;

	MICROJXL__ASSERT(tr->tr == MICROJXL__TR_SQUEEZE);
	MICROJXL__ASSERT(begin_c >= 0 && end_c < m->num_channels);
	MICROJXL__ASSERT(r > end_c && r < m->num_channels);

	for (c = begin_c; c <= end_c; ++c) {
		microjxl__plane *chan = &m->channel[c];
		microjxl__plane *res = &m->channel[r];
		microjxl__plane out;
		int32_t ow, oh;

		if (tr->sq.horizontal) {
			ow = chan->width + res->width;
			oh = chan->height;
		} else {
			ow = chan->width;
			oh = chan->height + res->height;
		}

		if (chan->type == MICROJXL__PLANE_EMPTY) {
			// nothing to reconstruct; keep the channel empty, restore the pre-squeeze shift
			microjxl__init_empty_plane(&out);
			out.hshift = (int8_t) (chan->hshift - (tr->sq.horizontal ? 1 : 0));
			out.vshift = (int8_t) (chan->vshift - (tr->sq.horizontal ? 0 : 1));
			m->channel[c] = out;
		} else if (res->type == MICROJXL__PLANE_EMPTY) {
			// residual has no pixels (the original dimension was 1): output == input
			out = *chan; // take over the pixels
			if (tr->sq.horizontal) out.hshift = (int8_t) (out.hshift - 1);
			else out.vshift = (int8_t) (out.vshift - 1);
			m->channel[c] = out;
		} else {
			MICROJXL__ASSERT(chan->type == MICROJXL__(PLANE_I,P) && res->type == MICROJXL__(PLANE_I,P));
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__(PLANE_I,P), ow, oh, MICROJXL__PLANE_FORCE_PAD, &out));
			out.hshift = (int8_t) (chan->hshift - (tr->sq.horizontal ? 1 : 0));
			out.vshift = (int8_t) (chan->vshift - (tr->sq.horizontal ? 0 : 1));
			if (tr->sq.horizontal) {
				MICROJXL__ASSERT(res->height == chan->height);
				for (y = 0; y < oh; ++y) {
					intP_t *avgline = MICROJXL__PIXELS(chan, y);
					intP_t *resline = MICROJXL__PIXELS(res, y);
					intP_t *outline = MICROJXL__PIXELS(&out, y);
					for (x = 0; x < res->width; ++x) {
						int2P_t avg = avgline[x], residu = resline[x];
						int2P_t next_avg = (x + 1 < chan->width ? avgline[x + 1] : avg);
						int2P_t left = (x > 0 ? outline[(x << 1) - 1] : avg);
						int2P_t diff = residu + microjxl__(tendency,P)(left, avg, next_avg);
						int2P_t first = avg + diff / 2;
						outline[x << 1] = (intP_t) first;
						outline[(x << 1) + 1] = (intP_t) (first - diff);
					}
					if (chan->width > res->width) outline[res->width << 1] = avgline[res->width];
				}
			} else {
				MICROJXL__ASSERT(res->width == chan->width);
				for (y = 0; y < res->height; ++y) {
					intP_t *avgline = MICROJXL__PIXELS(chan, y);
					intP_t *resline = MICROJXL__PIXELS(res, y);
					intP_t *navgline = MICROJXL__PIXELS(chan, y + 1 < chan->height ? y + 1 : y);
					intP_t *outline = MICROJXL__PIXELS(&out, y << 1);
					intP_t *noutline = MICROJXL__PIXELS(&out, (y << 1) + 1);
					intP_t *topline = (y > 0 ? MICROJXL__PIXELS(&out, (y << 1) - 1) : avgline);
					for (x = 0; x < ow; ++x) {
						int2P_t avg = avgline[x], residu = resline[x];
						int2P_t next_avg = navgline[x], top = topline[x];
						int2P_t diff = residu + microjxl__(tendency,P)(top, avg, next_avg);
						int2P_t first = avg + diff / 2;
						outline[x] = (intP_t) first;
						noutline[x] = (intP_t) (first - diff);
					}
				}
				if (chan->height > res->height) {
					intP_t *avgline = MICROJXL__PIXELS(chan, res->height);
					intP_t *outline = MICROJXL__PIXELS(&out, res->height << 1);
					for (x = 0; x < ow; ++x) outline[x] = avgline[x];
				}
			}
			microjxl__mem_free_plane(chan);
			m->channel[c] = out;
		}

		// remove the residual channel at index r (it was at index r for every c)
		microjxl__mem_free_plane(&m->channel[r]);
		memmove(m->channel + r, m->channel + r + 1,
			sizeof(microjxl__plane) * (size_t) (m->num_channels - r - 1));
		--m->num_channels;
	}

MICROJXL__ON_ERROR:
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

// ----------------------------------------
// end of recursion
	#undef microjxl__intP
	#undef microjxl__int2P
	#undef MICROJXL__PIXELS
	#undef MICROJXL__P
	#undef MICROJXL__2P
#endif // MICROJXL__RECURSING == 300
#if MICROJXL__RECURSING < 0
// ----------------------------------------

#ifdef MICROJXL_IMPLEMENTATION
MICROJXL__STATIC_RETURNS_ERR microjxl__inverse_transform(microjxl__st *st, microjxl__modular *m) {
	if (getenv("MICROJXL_DUMP_PRE2") && m->num_channels > 0 && m->num_channels < 1000 && m->channel) {
		static int prectr = 0;
		char pfn[64];
		snprintf(pfn, sizeof(pfn), "/tmp/mj_pre2_%d.bin", prectr++);
		FILE *pf = fopen(pfn, "wb");
		int32_t nc = m->num_channels, zz, xx, yy;
		fwrite(&nc, 4, 1, pf);
		for (zz = 0; zz < nc; ++zz) {
			microjxl__plane *pc = &m->channel[zz];
			int32_t w = (pc && pc->width > 0) ? pc->width : 0, h = (pc && pc->height > 0) ? pc->height : 0;
			int32_t hdr[4] = {w, h, pc ? pc->hshift : 0, pc ? pc->vshift : 0};
			fwrite(hdr, 4, 4, pf);
			for (yy = 0; yy < h; ++yy) for (xx = 0; xx < w; ++xx) {
				int32_t v = 0;
				if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx];
				else if (pc->type == MICROJXL__PLANE_I16) v = MICROJXL__I16_PIXELS(pc, yy)[xx];
				fwrite(&v, 4, 1, pf);
			}
		}
		fclose(pf);
	}
	int32_t i;

	if (m->num_channels == 0) return 0;

	switch (microjxl__plane_all_equal_typed_or_empty(m->channel, m->channel + m->num_channels)) {
	case MICROJXL__PLANE_I16:
		for (i = m->nb_transforms - 1; i >= 0; --i) {
			const microjxl__transform *tr = &m->transform[i];
			switch (tr->tr) {
			case MICROJXL__TR_RCT: microjxl__inverse_rct16(m, tr); break;
			case MICROJXL__TR_PALETTE: MICROJXL__TRY(microjxl__inverse_palette16(st, m, tr)); break;
			case MICROJXL__TR_SQUEEZE: MICROJXL__TRY(microjxl__inverse_squeeze16(st, m, tr)); break;
			default: MICROJXL__UNREACHABLE();
			}
		}
		break;

	case MICROJXL__PLANE_I32:
		for (i = m->nb_transforms - 1; i >= 0; --i) {
			const microjxl__transform *tr = &m->transform[i];
			switch (tr->tr) {
			case MICROJXL__TR_RCT: microjxl__inverse_rct32(m, tr); break;
			case MICROJXL__TR_PALETTE: MICROJXL__TRY(microjxl__inverse_palette32(st, m, tr)); break;
			case MICROJXL__TR_SQUEEZE: MICROJXL__TRY(microjxl__inverse_squeeze32(st, m, tr)); break;
			default: MICROJXL__UNREACHABLE();
			}
		}
		break;

	default: // while *some* channels can be empty, it is impossible that all channels are empty
		MICROJXL__UNREACHABLE();
	}

MICROJXL__ON_ERROR:
	return st->err;
}
#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// dequantization matrix and coefficient orders

enum {
	MICROJXL__NUM_DCT_SELECT = 27, // the number of all possible varblock types (DctSelect)
	MICROJXL__NUM_DCT_PARAMS = 17, // the number of parameters, some shared by multiple DctSelects
	MICROJXL__NUM_ORDERS = 13, // the number of distinct varblock dimensions & orders, after transposition
};

enum microjxl__dq_matrix_mode { // the number of params per channel follows:
	MICROJXL__DQ_ENC_LIBRARY = 0, // 0
	MICROJXL__DQ_ENC_HORNUSS = 1, // 3 (params)
	MICROJXL__DQ_ENC_DCT2 = 2, // 6 (params)
	MICROJXL__DQ_ENC_DCT4 = 3, // 2 (params) + n (dct_params)
	// TODO DCT4x8 uses an undefined name "parameters" (should be "params")
	MICROJXL__DQ_ENC_DCT4X8 = 4, // 1 (params) + n (dct_params)
	MICROJXL__DQ_ENC_AFV = 5, // 9 (params) + n (dct_params) + m (dct4x4_params)
	MICROJXL__DQ_ENC_DCT = 6, // n (params)
	// all other modes eventually decode to:
	MICROJXL__DQ_ENC_RAW = 7, // n rows * m columns, with the top-left 1/8 by 1/8 unused
};

typedef struct {
	enum microjxl__dq_matrix_mode mode;
	int16_t n, m;
	microjxl_f32x4 *params; // the last element per each row is unused
} microjxl__dq_matrix;

MICROJXL__STATIC_RETURNS_ERR microjxl__read_dq_matrix(
	microjxl__st *st, int32_t rows, int32_t columns, int64_t raw_sidx,
	microjxl__tree_node *global_tree, const microjxl__code_spec *global_codespec, microjxl__dq_matrix *dqmat
);
/* JPEG reconstruction: capture the RAW 8x8 quant table integers while they
 * are live in microjxl__read_dq_matrix (see microjxl.h jbrd section). */
MICROJXL_STATIC void microjxl__jpeg_recon_capture_qtable(
	microjxl__st *st, int32_t rows, int32_t columns, const microjxl__modular *m
);
MICROJXL_INLINE float microjxl__interpolate(float pos, int32_t c, const microjxl_f32x4 *bands, int32_t len);
MICROJXL__STATIC_RETURNS_ERR microjxl__interpolation_bands(
	microjxl__st *st, const microjxl_f32x4 *params, int32_t nparams, microjxl_f32x4 *out
);
MICROJXL_STATIC void microjxl__dct_quant_weights(
	int32_t rows, int32_t columns, const microjxl_f32x4 *bands, int32_t len, microjxl_f32x4 *out
);
MICROJXL__STATIC_RETURNS_ERR microjxl__load_dq_matrix(microjxl__st *st, int32_t idx, microjxl__dq_matrix *dqmat);
MICROJXL_STATIC void microjxl__mem_free_dq_matrix(microjxl__dq_matrix *dqmat);
MICROJXL__STATIC_RETURNS_ERR microjxl__natural_order(microjxl__st *st, int32_t log_rows, int32_t log_columns, int32_t **out);

#ifdef MICROJXL_IMPLEMENTATION

typedef struct { int8_t log_rows, log_columns, param_idx, order_idx; } microjxl__dct_select;
static const microjxl__dct_select MICROJXL__DCT_SELECT[MICROJXL__NUM_DCT_SELECT] = {
	// hereafter DCTnm refers to DCT(2^n)x(2^m) in the spec
	/*DCT33*/ {3, 3, 0, 0}, /*Hornuss*/ {3, 3, 1, 1}, /*DCT11*/ {3, 3, 2, 1}, /*DCT22*/ {3, 3, 3, 1},
	/*DCT44*/ {4, 4, 4, 2}, /*DCT55*/ {5, 5, 5, 3}, /*DCT43*/ {4, 3, 6, 4}, /*DCT34*/ {3, 4, 6, 4},
	/*DCT53*/ {5, 3, 7, 5}, /*DCT35*/ {3, 5, 7, 5}, /*DCT54*/ {5, 4, 8, 6}, /*DCT45*/ {4, 5, 8, 6},
	/*DCT23*/ {3, 3, 9, 1}, /*DCT32*/ {3, 3, 9, 1}, /*AFV0*/ {3, 3, 10, 1}, /*AFV1*/ {3, 3, 10, 1},
	/*AFV2*/ {3, 3, 10, 1}, /*AFV3*/ {3, 3, 10, 1}, /*DCT66*/ {6, 6, 11, 7}, /*DCT65*/ {6, 5, 12, 8},
	/*DCT56*/ {5, 6, 12, 8}, /*DCT77*/ {7, 7, 13, 9}, /*DCT76*/ {7, 6, 14, 10}, /*DCT67*/ {6, 7, 14, 10},
	/*DCT88*/ {8, 8, 15, 11}, /*DCT87*/ {8, 7, 16, 12}, /*DCT78*/ {7, 8, 16, 12},
};

static const struct microjxl__dct_params {
	int8_t log_rows, log_columns, def_offset, def_mode, def_n, def_m;
} MICROJXL__DCT_PARAMS[MICROJXL__NUM_DCT_PARAMS] = {
	/*DCT33*/ {3, 3, 0, MICROJXL__DQ_ENC_DCT, 6, 0}, /*Hornuss*/ {3, 3, 6, MICROJXL__DQ_ENC_HORNUSS, 0, 0},
	/*DCT11*/ {3, 3, 9, MICROJXL__DQ_ENC_DCT2, 0, 0}, /*DCT22*/ {3, 3, 15, MICROJXL__DQ_ENC_DCT4, 4, 0},
	/*DCT44*/ {4, 4, 21, MICROJXL__DQ_ENC_DCT, 7, 0}, /*DCT55*/ {5, 5, 28, MICROJXL__DQ_ENC_DCT, 8, 0},
	/*DCT34*/ {3, 4, 36, MICROJXL__DQ_ENC_DCT, 7, 0}, /*DCT35*/ {3, 5, 43, MICROJXL__DQ_ENC_DCT, 8, 0},
	/*DCT45*/ {4, 5, 51, MICROJXL__DQ_ENC_DCT, 8, 0}, /*DCT23*/ {3, 3, 59, MICROJXL__DQ_ENC_DCT4X8, 4, 0},
	/*AFV*/ {3, 3, 64, MICROJXL__DQ_ENC_AFV, 4, 4}, /*DCT66*/ {6, 6, 81, MICROJXL__DQ_ENC_DCT, 8, 0},
	/*DCT56*/ {5, 6, 89, MICROJXL__DQ_ENC_DCT, 8, 0}, /*DCT77*/ {7, 7, 97, MICROJXL__DQ_ENC_DCT, 8, 0},
	/*DCT67*/ {6, 7, 105, MICROJXL__DQ_ENC_DCT, 8, 0}, /*DCT88*/ {8, 8, 113, MICROJXL__DQ_ENC_DCT, 8, 0},
	/*DCT78*/ {7, 8, 121, MICROJXL__DQ_ENC_DCT, 8, 0},
};

#define MICROJXL__DCT4X4_DCT_PARAMS \
	{2200.0f, 392.0f, 112.0f}, {0.0f, 0.0f, -0.25f}, {0.0f, 0.0f, -0.25f}, {0.0f, 0.0f, -0.5f} // (4)
#define MICROJXL__DCT4X8_DCT_PARAMS \
	{2198.050556016380522f, 764.3655248643528689f, 527.107573587542228f}, \
	{-0.96269623020744692f, -0.92630200888366945f, -1.4594385811273854f}, \
	{-0.76194253026666783f, -0.9675229603596517f, -1.450082094097871593f}, \
	{-0.6551140670773547f, -0.27845290869168118f, -1.5843722511996204f} // (4)
#define MICROJXL__LARGE_DCT_PARAMS_BASE(mult, bx, by, bb) \
	/* it turns out that the first sets of parameters for larger DCTs have the same */ \
	/* ratios (only band 0 differs); libjxl quant_weights.cc uses base 26629.07/ */ \
	/* 9311.32/4992.25 for the square tables (64x64, 128x128, 256x256) and base */ \
	/* 23629.07/8611.32/4492.25 for the rectangular ones (32x64, 64x128, 128x256). */ \
	{mult * bx, mult * by, mult * bb}, \
	{-1.025f, -0.3041958212306401f, -1.2f}, {-0.78f, -0.3633036457487539f, -1.2f}, \
	{-0.65012f, -0.35660379990111464f, -0.8f}, {-0.19041574084286472f, -0.3443074455424403f, -0.7f}, \
	{-0.20819395464f, -0.33699592683512467f, -0.7f}, {-0.421064f, -0.30180866526242109f, -0.4f}, \
	{-0.32733845535848671f, -0.27321683125358037f, -0.5f} // (8)
#define MICROJXL__LARGE_DCT_PARAMS(mult) \
	MICROJXL__LARGE_DCT_PARAMS_BASE(mult, 23629.073922049845f, 8611.3238710010046f, 4492.2486445538634f)
#define MICROJXL__LARGE_DCT_PARAMS_SQ(mult) \
	MICROJXL__LARGE_DCT_PARAMS_BASE(mult, 26629.073922049845f, 9311.3238710010046f, 4992.2486445538634f)
static const float MICROJXL__LIBRARY_DCT_PARAMS[129][4] = {
	// DCT33 dct_params (n=6) (SPEC some values are incorrect)
	{3150.0f, 560.0f, 512.0f}, {0.0f, 0.0f, -2.0f}, {-0.4f, -0.3f, -1.0f},
	{-0.4f, -0.3f, 0.0f}, {-0.4f, -0.3f, -1.0f}, {-2.0f, -0.3f, -2.0f},
	// Hornuss params (3)
	{280.0f, 60.0f, 18.0f}, {3160.0f, 864.0f, 200.0f}, {3160.0f, 864.0f, 200.0f},
	// DCT11 params (6)
	{3840.0f, 960.0f, 640.0f}, {2560.0f, 640.0f, 320.0f}, {1280.0f, 320.0f, 128.0f},
	{640.0f, 180.0f, 64.0f}, {480.0f, 140.0f, 32.0f}, {300.0f, 120.0f, 16.0f},
	// DCT22 params (2) + dct_params (n=4) (TODO spec bug: some values are incorrect)
	{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, MICROJXL__DCT4X4_DCT_PARAMS,
	// DCT44 dct_params (n=7)
	{8996.8725711814115328f, 3191.48366296844234752f, 1157.50408145487200256f},
	{-1.3000777393353804f, -0.67424582104194355f, -2.0531423165804414f},
	{-0.49424529824571225f, -0.80745813428471001f, -1.4f},
	{-0.439093774457103443f, -0.44925837484843441f, -0.50687130033378396f},
	{-0.6350101832695744f, -0.35865440981033403f, -0.42708730624733904f},
	{-0.90177264050827612f, -0.31322389111877305f, -1.4856834539296244f},
	{-1.6162099239887414f, -0.37615025315725483f, -4.9209142884401604f},
	// DCT55 dct_params (n=8)
	{15718.40830982518931456f, 7305.7636810695983104f, 3803.53173721215041536f},
	{-1.025f, -0.8041958212306401f, -3.060733579805728f},
	{-0.98f, -0.7633036457487539f, -2.0413270132490346f},
	{-0.9012f, -0.55660379990111464f, -2.0235650159727417f},
	{-0.4f, -0.49785304658857626f, -0.5495389509954993f},
	{-0.48819395464f, -0.43699592683512467f, -0.4f},
	{-0.421064f, -0.40180866526242109f, -0.4f},
	{-0.27f, -0.27321683125358037f, -0.3f},
	// DCT34 dct_params (n=7)
	{7240.7734393502f, 1448.15468787004f, 506.854140754517f},
	{-0.7f, -0.5f, -1.4f}, {-0.7f, -0.5f, -0.2f}, {-0.2f, -0.5f, -0.5f},
	{-0.2f, -0.2f, -0.5f}, {-0.2f, -0.2f, -1.5f}, {-0.5f, -0.2f, -3.6f},
	// DCT35 dct_params (n=8)
	{16283.2494710648897f, 5089.15750884921511936f, 3397.77603275308720128f},
	{-1.7812845336559429f, -0.320049391452786891f, -0.321327362693153371f},
	{-1.6309059012653515f, -0.35362849922161446f, -0.34507619223117997f},
	{-1.0382179034313539f, -0.30340000000000003f, -0.70340000000000003f},
	{-0.85f, -0.61f, -0.9f}, {-0.7f, -0.5f, -1.0f}, {-0.9f, -0.5f, -1.0f},
	{-1.2360638576849587f, -0.6f, -1.1754605576265209f},
	// DCT45 dct_params (n=8)
	{13844.97076442300573f, 4798.964084220744293f, 1807.236946760964614f},
	{-0.97113799999999995f, -0.61125308982767057f, -1.2f},
	{-0.658f, -0.83770786552491361f, -1.2f}, {-0.42026f, -0.79014862079498627f, -0.7f},
	{-0.22712f, -0.2692727459704829f, -0.7f}, {-0.2206f, -0.38272769465388551f, -0.7f},
	{-0.226f, -0.22924222653091453f, -0.4f}, {-0.6f, -0.20719098826199578f, -0.5f},
	// DCT23 params (1) + dct_params (n=4)
	{1.0f, 1.0f, 1.0f}, MICROJXL__DCT4X8_DCT_PARAMS,
	// AFV params (9) + dct_params (n=4) + dct4x4_params (m=4)
	// (SPEC params & dct_params are swapped; TODO spec bug: dct4x4_params are also incorrect)
	{3072.0f, 1024.0f, 384.0f}, {3072.0f, 1024.0f, 384.0f}, {256.0f, 50.0f, 12.0f},
	{256.0f, 50.0f, 12.0f}, {256.0f, 50.0f, 12.0f}, {414.0f, 58.0f, 22.0f},
	{0.0f, 0.0f, -0.25f}, {0.0f, 0.0f, -0.25f}, {0.0f, 0.0f, -0.25f},
	MICROJXL__DCT4X8_DCT_PARAMS, MICROJXL__DCT4X4_DCT_PARAMS,

	MICROJXL__LARGE_DCT_PARAMS_SQ(0.9f), // DCT66 dct_params (n=8)
	MICROJXL__LARGE_DCT_PARAMS(0.65f), // DCT56 dct_params (n=8)
	MICROJXL__LARGE_DCT_PARAMS_SQ(1.8f), // DCT77 dct_params (n=8)
	MICROJXL__LARGE_DCT_PARAMS(1.3f), // DCT67 dct_params (n=8)
	MICROJXL__LARGE_DCT_PARAMS_SQ(3.6f), // DCT88 dct_params (n=8)
	MICROJXL__LARGE_DCT_PARAMS(2.6f), // DCT78 dct_params (n=8)
};

static const int8_t MICROJXL__LOG_ORDER_SIZE[MICROJXL__NUM_ORDERS][2] = {
	{3,3}, {3,3}, {4,4}, {5,5}, {3,4}, {3,5}, {4,5}, {6,6}, {5,6}, {7,7}, {6,7}, {8,8}, {7,8},
};

MICROJXL__STATIC_RETURNS_ERR microjxl__read_dq_matrix(
	microjxl__st *st, int32_t rows, int32_t columns, int64_t raw_sidx,
	microjxl__tree_node *global_tree, const microjxl__code_spec *global_codespec, microjxl__dq_matrix *dqmat
) {
	microjxl__modular m = MICROJXL__INIT;
	int32_t c, i, j;

	dqmat->mode = (enum microjxl__dq_matrix_mode) microjxl__u(st, 3);
	dqmat->params = NULL;
	if (dqmat->mode == MICROJXL__DQ_ENC_RAW) { // read as a modular image
		float denom;
		int32_t w[3], h[3], x, y;

		denom = microjxl__f16(st);
		// TODO spec bug: ZeroPadToByte isn't required at this point
		MICROJXL__SHOULD(microjxl__surely_nonzero(denom), "dqm0");

		w[0] = w[1] = w[2] = columns;
		h[0] = h[1] = h[2] = rows;
		MICROJXL__TRY(microjxl__init_modular(st, 3, w, h, &m));
		MICROJXL__TRY(microjxl__modular_header(st, global_tree, global_codespec, &m));
		MICROJXL__TRY(microjxl__allocate_modular(st, &m));
		for (c = 0; c < m.num_channels; ++c) MICROJXL__TRY(microjxl__modular_channel(st, &m, c, raw_sidx));
		MICROJXL__TRY(microjxl__finish_and_free_code(st, &m.code));
		MICROJXL__TRY(microjxl__inverse_transform(st, &m));

		/* JPEG reconstruction: the raw integers are still live here */
		microjxl__jpeg_recon_capture_qtable(st, rows, columns, &m);

		MICROJXL__TRY_MALLOC(microjxl_f32x4, &dqmat->params, (size_t) (rows * columns));
		/* spec 18181-1 1.2.4: for encoding mode RAW the dequantization matrix
		 * is params * denominator (i.e. qtable * denom), so the weight stored
		 * here (which the HF dequant divides by) is its reciprocal
		 * 1 / (qtable * denom). This matches libjxl's RAW handling. */
		for (c = 0; c < 3; ++c) {
			if (m.channel[c].type == MICROJXL__PLANE_I16) {
				for (y = 0; y < rows; ++y) {
					int16_t *pixels = MICROJXL__I16_PIXELS(&m.channel[c], y);
					for (x = 0; x < columns; ++x) {
						dqmat->params[y * columns + x][c] = 1.0f / ((float) pixels[x] * denom);
					}
				}
			} else {
				for (y = 0; y < rows; ++y) {
					int32_t *pixels = MICROJXL__I32_PIXELS(&m.channel[c], y);
					for (x = 0; x < columns; ++x) {
						dqmat->params[y * columns + x][c] = 1.0f / ((float) pixels[x] * denom);
					}
				}
			}
		}

		microjxl__mem_free_modular(&m);
		dqmat->n = (int16_t) rows;
		dqmat->m = (int16_t) columns;
	} else {
		static const struct how {
			int8_t requires8x8; // 1 if an 8x8 (one-block) matrix is required;
			// modes 1-5 only (libjxl required_size != 1 checks). Mode 6
			// (kQuantModeDCT) is legal for any table size: it scales the
			// matrix from num_distance_bands distance bands, so a 16x16
			// RAW-sized table with DCT params is valid (cjxl emits this for
			// VarDCT DC frames). Mode 7 (kQuantModeRAW) reads an explicit
			// modular table of the full required size.
			int8_t nparams; // the number of fixed parameters
			int8_t nscaled; // params[0..nscaled-1] should be scaled by 64
			int8_t ndctparams; // the number of calls to ReadDctParams
		} HOW[7] = {{0,0,0,0}, {1,3,3,0}, {1,6,6,0}, {1,2,2,1}, {1,1,0,1}, {1,9,6,2}, {0,0,0,1}};
		struct how how = HOW[dqmat->mode];
		int32_t paramsize = how.nparams + how.ndctparams * 16, paramidx = how.nparams;
		if (how.requires8x8) {
#ifdef MICROJXL_DEBUG
			if (rows != 8 || columns != 8) fprintf(stderr, "[microjxl] dqm? mode=%d rows=%d columns=%d bits=%lld\n", (int) dqmat->mode, rows, columns, (long long) microjxl__bits_read(st));
#endif
			MICROJXL__SHOULD(rows == 8 && columns == 8, "dqm?");
		}
		if (paramsize) {
			MICROJXL__TRY_MALLOC(microjxl_f32x4, &dqmat->params, (size_t) paramsize);
			for (c = 0; c < 3; ++c) for (j = 0; j < how.nparams; ++j) {
				dqmat->params[j][c] = microjxl__f16(st) * (j < how.nscaled ? 64.0f : 1.0f);
			}
			for (i = 0; i < how.ndctparams; ++i) { // ReadDctParams
				int32_t n = *(i == 0 ? &dqmat->n : &dqmat->m) = (int16_t) (microjxl__u(st, 4) + 1);
				for (c = 0; c < 3; ++c) for (j = 0; j < n; ++j) {
					dqmat->params[paramidx + j][c] = microjxl__f16(st) * (j == 0 ? 64.0f : 1.0f);
				}
				paramidx += n;
			}
		}
		MICROJXL__RAISE_DELAYED();
	}
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(dqmat->params);
	dqmat->params = NULL;
	microjxl__mem_free_modular(&m);
	return st->err;
}

// piecewise exponential interpolation where pos is in [0,1], mapping pos = k/(len-1) to bands[k]
MICROJXL_INLINE float microjxl__interpolate(float pos, int32_t c, const microjxl_f32x4 *bands, int32_t len) {
	float scaled_pos, frac_idx, a, b;
	int32_t scaled_idx;
	if (len == 1) return bands[0][c];
	scaled_pos = pos * (float) (len - 1);
	scaled_idx = (int32_t) scaled_pos;
	frac_idx = scaled_pos - (float) scaled_idx;
	a = bands[scaled_idx][c];
	b = bands[scaled_idx + 1][c];
	return a * powf(b / a, frac_idx);
}

MICROJXL__STATIC_RETURNS_ERR microjxl__interpolation_bands(
	microjxl__st *st, const microjxl_f32x4 *params, int32_t nparams, microjxl_f32x4 *out
) {
	int32_t i, c;
	for (c = 0; c < 3; ++c) {
		// TODO spec bug: loops for x & y are independent of the loop for i (bands)
		// TODO spec bug: `bands(i)` for i >= 0 (not i > 0) should be larger (not no less) than 0
		out[0][c] = params[0][c];
		MICROJXL__SHOULD(out[0][c] > 0, "band");
		for (i = 1; i < nparams; ++i) {
			float v = params[i][c];
			out[i][c] = v > 0 ? out[i - 1][c] * (1.0f + v) : out[i - 1][c] / (1.0f - v);
			MICROJXL__SHOULD(out[i][c] > 0, "band");
		}
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_STATIC void microjxl__dct_quant_weights(
	int32_t rows, int32_t columns, const microjxl_f32x4 *bands, int32_t len, microjxl_f32x4 *out
) {
	float inv_rows_m1 = 1.0f / (float) (rows - 1), inv_columns_m1 = 1.0f / (float) (columns - 1);
	int32_t x, y, c;
	for (c = 0; c < 3; ++c) {
		for (y = 0; y < rows; ++y) for (x = 0; x < columns; ++x) {
			static const float INV_SQRT2 = 1.0f / 1.414214562373095f; // 1/(sqrt(2) + 1e-6)
			float d = hypotf((float) x * inv_columns_m1, (float) y * inv_rows_m1);
			// TODO spec issue: num_bands doesn't exist (probably len)
			out[y * columns + x][c] = microjxl__interpolate(d * INV_SQRT2, c, bands, len);
		}
	}
}

// TODO spec issue: VarDCT uses the (row, column) notation, not the (x, y) notation; explicitly note this
// TODO spec improvement: spec can provide computed matrices for default parameters to aid verification
MICROJXL__STATIC_RETURNS_ERR microjxl__load_dq_matrix(microjxl__st *st, int32_t idx, microjxl__dq_matrix *dqmat) {
	enum { MAX_BANDS = 15 };
	const struct microjxl__dct_params dct = MICROJXL__DCT_PARAMS[idx];
	enum microjxl__dq_matrix_mode mode;
	int32_t rows, columns, n, m;
	const microjxl_f32x4 *params;
	microjxl_f32x4 *raw = NULL, bands[MAX_BANDS], scratch[64];
	int32_t x, y, i, c;

	mode = dqmat->mode;
	if (mode == MICROJXL__DQ_ENC_RAW) {
		return 0; // nothing to do
	} else if (mode == MICROJXL__DQ_ENC_LIBRARY) {
		mode = (enum microjxl__dq_matrix_mode) dct.def_mode;
		n = dct.def_n;
		m = dct.def_m;
		params = MICROJXL__LIBRARY_DCT_PARAMS + dct.def_offset;
	} else {
		n = dqmat->n;
		m = dqmat->m;
		params = (const microjxl_f32x4 *) dqmat->params;
	}

	rows = 1 << dct.log_rows;
	columns = 1 << dct.log_columns;
	MICROJXL__TRY_MALLOC(microjxl_f32x4, &raw, (size_t) (rows * columns));

	switch (mode) {
	case MICROJXL__DQ_ENC_DCT:
		MICROJXL__TRY(microjxl__interpolation_bands(st, params, n, bands));
		microjxl__dct_quant_weights(rows, columns, (const microjxl_f32x4 *) bands, n, raw);
		break;

	case MICROJXL__DQ_ENC_DCT4:
		MICROJXL__ASSERT(rows == 8 && columns == 8);
		MICROJXL__ASSERT(n <= MAX_BANDS);
		MICROJXL__TRY(microjxl__interpolation_bands(st, params + 2, n, bands));
		microjxl__dct_quant_weights(4, 4, (const microjxl_f32x4 *) bands, n, scratch);
		for (c = 0; c < 3; ++c) {
			for (y = 0; y < 8; ++y) for (x = 0; x < 8; ++x) {
				raw[y * 8 + x][c] = scratch[(y / 2) * 4 + (x / 2)][c];
			}
			raw[001][c] /= params[0][c];
			raw[010][c] /= params[0][c];
			raw[011][c] /= params[1][c];
		}
		break;

	case MICROJXL__DQ_ENC_DCT2:
		MICROJXL__ASSERT(rows == 8 && columns == 8);
		for (c = 0; c < 3; ++c) {
			static const int8_t MAP[64] = {
				// TODO spec issue: coefficient (0,0) is unspecified; means it shouldn't be touched
				0,0,2,2,4,4,4,4,
				0,1,2,2,4,4,4,4,
				2,2,3,3,4,4,4,4,
				2,2,3,3,4,4,4,4,
				4,4,4,4,5,5,5,5,
				4,4,4,4,5,5,5,5,
				4,4,4,4,5,5,5,5,
				4,4,4,4,5,5,5,5,
			};
			for (i = 0; i < 64; ++i) raw[i][c] = params[MAP[i]][c];
			raw[0][c] = -1.0f;
		}
		break;

	case MICROJXL__DQ_ENC_HORNUSS:
		MICROJXL__ASSERT(rows == 8 && columns == 8);
		for (c = 0; c < 3; ++c) {
			for (i = 0; i < 64; ++i) raw[i][c] = params[0][c];
			raw[000][c] = 1.0f;
			raw[001][c] = raw[010][c] = params[1][c];
			raw[011][c] = params[2][c];
		}
		break;

	case MICROJXL__DQ_ENC_DCT4X8:
		MICROJXL__ASSERT(rows == 8 && columns == 8);
		MICROJXL__ASSERT(n <= MAX_BANDS);
		MICROJXL__TRY(microjxl__interpolation_bands(st, params + 1, n, bands));
		// TODO spec bug: 4 rows by 8 columns, not 8 rows by 4 columns (compare with AFV weights4x8)
		// the position (x, y Idiv 2) is also confusing, since it's using the (x, y) notation
		microjxl__dct_quant_weights(4, 8, (const microjxl_f32x4 *) bands, n, scratch);
		for (c = 0; c < 3; ++c) {
			for (y = 0; y < 8; ++y) for (x = 0; x < 8; ++x) {
				raw[y * 8 + x][c] = scratch[(y / 2) * 8 + x][c];
			}
			raw[001][c] /= params[0][c];
		}
		break;

	case MICROJXL__DQ_ENC_AFV:
		MICROJXL__ASSERT(rows == 8 && columns == 8);
		MICROJXL__ASSERT(n <= MAX_BANDS && m <= MAX_BANDS);
		MICROJXL__TRY(microjxl__interpolation_bands(st, params + 9, n, bands));
		microjxl__dct_quant_weights(4, 8, (const microjxl_f32x4 *) bands, n, scratch);
		MICROJXL__TRY(microjxl__interpolation_bands(st, params + 9 + n, m, bands));
		microjxl__dct_quant_weights(4, 4, (const microjxl_f32x4 *) bands, m, scratch + 32);
		MICROJXL__TRY(microjxl__interpolation_bands(st, params + 5, 4, bands));
		for (c = 0; c < 3; ++c) {
			// TODO spec bug: this value can never be 1 because it will result in an out-of-bound
			// access in microjxl__interpolate; libjxl avoids this by adding 1e-6 to the denominator
			static const float FREQS[12] = { // precomputed values of (freqs[i] - lo) / (hi - lo + 1e-6)
				0.000000000f, 0.373436417f, 0.320380100f, 0.379332596f, 0.066671353f, 0.259756761f,
				0.530035651f, 0.789731061f, 0.149436598f, 0.559318823f, 0.669198646f, 0.999999917f,
			};
			scratch[0][c] = params[0][c]; // replaces the top-left corner of weights4x8
			scratch[32][c] = params[1][c]; // replaces the top-left corner of weights4x4
			for (i = 0; i < 12; ++i) scratch[i + 48][c] = microjxl__interpolate(FREQS[i], c, (const microjxl_f32x4 *) bands, 4);
			scratch[60][c] = 1.0f;
			for (i = 0; i < 3; ++i) scratch[i + 61][c] = params[i + 2][c];
		}
		for (c = 0; c < 3; ++c) {
			// TODO spec bug: `weight(...)` uses multiple conflicting notations
			static const int8_t MAP[64] = {
				// 1..31 from weights4x8, 33..47 from weights4x4, 48..59 interpolated,
				// 0/32/61..63 directly from parameters, 60 fixed to 1.0
				60, 32, 62, 33, 48, 34, 49, 35,
				 0,  1,  2,  3,  4,  5,  6,  7,
				61, 36, 63, 37, 50, 38, 51, 39,
				 8,  9, 10, 11, 12, 13, 14, 15,
				52, 40, 53, 41, 54, 42, 55, 43,
				16, 17, 18, 19, 20, 21, 22, 23,
				56, 44, 57, 45, 58, 46, 59, 47,
				24, 25, 26, 27, 28, 29, 30, 31,
			};
			for (i = 0; i < 64; ++i) raw[i][c] = scratch[MAP[i]][c];
		}
		break;

	default: MICROJXL__UNREACHABLE();
	}

	microjxl__mem_free(dqmat->params);
	dqmat->mode = MICROJXL__DQ_ENC_RAW;
	dqmat->n = (int16_t) rows;
	dqmat->m = (int16_t) columns;
	dqmat->params = raw;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(raw);
	return st->err;
}

MICROJXL_STATIC void microjxl__mem_free_dq_matrix(microjxl__dq_matrix *dqmat) {
	if (dqmat->mode != MICROJXL__DQ_ENC_LIBRARY) microjxl__mem_free(dqmat->params);
	dqmat->mode = MICROJXL__DQ_ENC_LIBRARY;
	dqmat->params = NULL;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__natural_order(microjxl__st *st, int32_t log_rows, int32_t log_columns, int32_t **out) {
	int32_t size = 1 << (log_rows + log_columns), log_slope = log_columns - log_rows;
	int32_t rows8 = 1 << (log_rows - 3), columns8 = 1 << (log_columns - 3);
	int32_t *order = NULL;
	int32_t y, x, key1, o;

	MICROJXL__ASSERT(8 >= log_columns && log_columns >= log_rows && log_rows >= 3);

	MICROJXL__TRY_MALLOC(int32_t, &order, (size_t) size);

	o = 0;
	for (y = 0; y < rows8; ++y) for (x = 0; x < columns8; ++x) {
		order[o++] = y << log_columns | x;
	}

	//            d e..
	// +---------/-/-  each diagonal is identified by an integer
	// |       |/ / /    key1 = scaled_x + scaled_y = x + y * 2^log_slope,
	// |_a_b_c_| / /   and covers at least one cell when:
	// |/ / / / / / /    2^(log_columns - 3) <= key1 < 2^(log_columns + 1) - 2^log_slope.
	for (key1 = 1 << (log_columns - 3); o < size; ++key1) {
		// place initial endpoints to leftmost and topmost edges, then fix out-of-bounds later
		int32_t x0 = key1 & ((1 << log_slope) - 1), y0 = key1 >> log_slope, x1 = key1, y1 = 0;
		if (x1 >= (1 << log_columns)) {
			int32_t excess = microjxl__ceil_div32(x1 - ((1 << log_columns) - 1), 1 << log_slope);
			x1 -= excess << log_slope;
			y1 += excess;
			MICROJXL__ASSERT(x1 >= 0 && y1 < (1 << log_rows));
		}
		if (y0 >= (1 << log_rows)) {
			int32_t excess = y0 - ((1 << log_rows) - 1);
			x0 += excess << log_slope;
			y0 -= excess;
			MICROJXL__ASSERT(x0 < (1 << log_columns) && y0 >= 0);
		}
		MICROJXL__ASSERT(o + (y0 - y1 + 1) <= size);
		if (key1 & 1) {
			for (x = x1, y = y1; x >= x0; x -= 1 << log_slope, ++y) {
				// skip the already covered top-left LLF region
				if (y >= rows8 || x >= columns8) order[o++] = y << log_columns | x;
			}
		} else {
			for (x = x0, y = y0; x <= x1; x += 1 << log_slope, --y) {
				if (y >= rows8 || x >= columns8) order[o++] = y << log_columns | x;
			}
		}
	}
	MICROJXL__ASSERT(o == size);

	*out = order;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(order);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// frame context

enum {
	MICROJXL__MAX_PASSES = 11,
};

enum {
	MICROJXL__BLEND_REPLACE = 0, // new
	MICROJXL__BLEND_ADD = 1,     // old + new
	MICROJXL__BLEND_BLEND = 2,   // new + old * (1 - new alpha) or equivalent, optionally clamped
	MICROJXL__BLEND_MUL_ADD = 3, // old + new * alpha or equivalent, optionally clamped
	MICROJXL__BLEND_MUL = 4,     // old * new, optionally clamped
};
typedef struct {
	int8_t mode, alpha_chan, clamp, src_ref_frame;
} microjxl__blend_info;

// splines (K.4)
typedef struct { float x, y; } microjxl__spline_pt;
// a spline as decoded (quantized DCTs, double-delta encoded control points)
typedef struct {
	int32_t num_points; // number of delta-delta control point pairs
	int64_t *points; // [num_points * 2] {dx, dy} delta-deltas
	int32_t dct[4][32]; // 0=X color, 1=Y color, 2=B color, 3=sigma
} microjxl__spline_q;
// one drawable Gaussian unit of the draw cache (libjxl SplineSegment)
typedef struct {
	float center_x, center_y, maximum_distance, inv_sigma;
	float sigma_over_4_times_intensity;
	float color[3];
} microjxl__spline_seg;

enum microjxl__frame_type {
	MICROJXL__FRAME_REGULAR = 0, MICROJXL__FRAME_LF = 1, MICROJXL__FRAME_REFONLY = 2, MICROJXL__FRAME_REGULAR_SKIPPROG = 3
};

// one blending directive for one channel of one patch (libjxl PatchBlending)
typedef struct {
	int32_t mode; // 0..7, libjxl PatchBlendMode
	int32_t alpha_channel;
	int clamp;
} microjxl__patch_blend;

typedef struct microjxl__patch_dict {
	microjxl__code_spec codespec; // kept for re-entry; freed after decode completes
	int codespec_valid;

	int32_t blendings_stride; // num_ec + 1
	int64_t num_ref, num_pos;
	// reference positions, [num_ref]
	int32_t *ref_idx; // reference frame slot 0..3
	int32_t *ref_x0, *ref_y0, *ref_xs, *ref_ys;
	// patch positions, [num_pos]
	int32_t *pos_ref; // index into ref_* arrays
	int32_t *pos_x, *pos_y;
	microjxl__patch_blend *blend; // [num_pos * blendings_stride]
} microjxl__patch_dict;

typedef struct microjxl__frame_st {
	int is_last;
	enum microjxl__frame_type type;
	int is_modular; // VarDCT if false
	int has_noise, has_patches, has_splines, use_lf_frame, skip_adapt_lf_smooth;
	int do_ycbcr;
	int32_t jpeg_upsampling; // [0] | [1] << 2 | [2] << 4
	// per-channel chroma subsampling shifts (log2), derived from jpeg_upsampling
	// (indexed by XYB channel: 0=X/Cb, 1=Y, 2=B/Cr), spec 18181-1 L.2
	int32_t jpeg_hshift[3], jpeg_vshift[3];
	int32_t log_upsampling, *ec_log_upsampling;
	int32_t group_size_shift;
	int32_t x_qm_scale, b_qm_scale;
	int32_t num_passes;
	int8_t shift[MICROJXL__MAX_PASSES];
	int8_t log_ds[MICROJXL__MAX_PASSES + 1]; // pass i shift range is [log_ds[i+1], log_ds[i])
	/* raw (downsample, last_pass) pairs for multi-pass frames; needed for
	 * libjxl's GetDownsamplingBracket (the log_ds derivation above is not
	 * sufficient to recover the per-pass channel filter) */
	int8_t num_ds;
	int8_t ds_log[MICROJXL__MAX_PASSES]; // log2(downsample) per pair
	int8_t ds_last_pass[MICROJXL__MAX_PASSES];
	int32_t lf_level;
	int32_t x0, y0, width, height;
	/* when log_upsampling > 0 the frame is stored at a reduced resolution
	 * (ceil(width/2^log_upsampling)) and upsampled to the final size after
	 * decoding; these hold the final (upsampled) dimensions */
	int32_t upsampled_width, upsampled_height;
	int32_t grows, gcolumns, ggrows, ggcolumns;
	// there can be at most (2^23 + 146)^2 groups and (2^20 + 29)^2 LF groups in a single frame
	int64_t num_groups, num_lf_groups;
	int64_t duration, timecode;
	microjxl__blend_info blend_info, *ec_blend_info;
	int32_t save_as_ref;
	int save_before_ct;
	int32_t name_len;
	char *name;
	struct {
		int enabled;
		float weights[3 /*xyb*/][2 /*0=weight1 (cardinal/center), 1=weight2 (diagonal/center)*/];
	} gab;
	struct {
		int32_t iters;
		float sharp_lut[8], channel_scale[3];
		float quant_mul, pass0_sigma_scale, pass2_sigma_scale, border_sad_mul, sigma_for_modular;
	} epf;
	// TODO spec bug: m_*_lf_unscaled are wildly incorrect, both in default values and scaling
	float m_lf_scaled[3 /*xyb*/];
	microjxl__tree_node *global_tree;
	microjxl__code_spec global_codespec;

	// modular only, available after LfGlobal (local groups are always pasted into gmodular)
	microjxl__modular gmodular;
	int32_t num_gm_channels; // <= gmodular.num_channels

	// vardct only, available after LfGlobal
	int32_t global_scale, quant_lf;
	int32_t lf_thr[3 /*xyb*/][15], qf_thr[15];
	int32_t nb_lf_thr[3 /*xyb*/], nb_qf_thr;
	uint8_t *block_ctx_map;
	int32_t block_ctx_size, nb_block_ctx;
	float inv_colour_factor;
	int32_t x_factor_lf, b_factor_lf;
	float base_corr_x, base_corr_b;

	/* JPEG reconstruction (Part 2 §9.10): per-frame capture state, filled
	 * from the container's jbrd box (microjxl__jpeg_reconstruct). Enabled
	 * for non-XYB VarDCT regular frames when the container carried jbrd.
	 * libjxl dec_group.cc captures the same data in DecodeGroupVarDCT:
	 * raw integer AC coefficients (pre-dequant, transposed), raw LfQuant
	 * integers (jbrd DC path uses quantizer.ClearDCMul, so the JPEG DC is
	 * the raw LfQuant integer minus the DC offset), and the RAW quant
	 * table integers (den == 1/(8*255)). */
	int jpeg_recon; // capture active for this frame
	int jpeg_recon_alloc; // jpeg_coeffs/jpeg_dcv hold allocations
	int jpeg_qtable_ok; // jpeg_qtable recovered from the RAW dq matrix
	int32_t jpeg_dc_prec_shift; // LfQuant extra_precision (mul = 2^-shift)
	int32_t jpeg_qtable[3 * 64]; // RAW qtable integers (XYB order, row-major)
	int32_t jpeg_wib[3]; // per-channel width in blocks (ceil(ceil(W/8)/2^hshift))
	int32_t jpeg_hib[3]; // per-channel height in blocks
	int32_t jpeg_dcv_w[3], jpeg_dcv_h[3]; // raw LfQuant block grid dims per channel
	int16_t *jpeg_coeffs[3]; // [jpeg_wib[c] * hib * 64] JXL (transposed) order
	int32_t *jpeg_dcv[3]; // raw LfQuant integers, frame block grid (XYB)

	// vardct only, available after HfGlobal/HfPass
	int32_t dct_select_used, dct_select_loaded; // i-th bit for DctSelect i
	int32_t order_used, order_loaded; // i-th bit for order i
	microjxl__dq_matrix dq_matrix[MICROJXL__NUM_DCT_PARAMS];
	int32_t num_hf_presets;
	// Lehmer code + sentinel (-1) before actual coefficient decoding,
	// either properly computed or discarded due to non-use later (can be NULL in that case)
	int32_t *orders[MICROJXL__MAX_PASSES][MICROJXL__NUM_ORDERS][3 /*xyb*/];
	microjxl__code_spec coeff_codespec[MICROJXL__MAX_PASSES];

	// noise (K.5): the LUT is decoded in LfGlobal; the three convolved
	// pseudorandom planes (full frame size) are generated lazily by
	// microjxl__add_noise_frame and consumed by the XYB pixel loops
	float noise_lut[8];
	microjxl__plane noise_planes[3];
	int noise_ready; // noise_planes hold the generated+convolved values

	// splines (K.4): quantized splines are decoded in LfGlobal; the draw
	// cache (per-row Gaussian segments) is built after LfChannelCorrelation
	// so dequantization sees the base correlations (defaults for modular)
	int32_t spline_qadjust; // quantization adjustment (unpacked signed)
	int32_t num_splines;
	microjxl__spline_q *splines;
	microjxl__spline_pt *starting_points;
	microjxl__spline_seg *segments;
	int32_t num_segments;
	int32_t *segment_indices; // [total] segment index per row slot
	int32_t *segment_y_start; // [spline_cache_h + 2] row -> [start, end) into segment_indices
	int32_t spline_cache_h; // frame height the cache was built for (stored resolution)

	// patches (K.3), decoded in LfGlobal when has_patches
	microjxl__patch_dict patches;

	/* Per-pixel XYB snapshot of this frame at the reference-save point
	 * (pre-colour-transform, post-noise, at stored resolution), filled
	 * by the combine pixel loops when the frame can be referenced; used
	 * by save_ref_frame. For upsampled VarDCT XYB frames it doubles as
	 * the deferred-conversion carrier: upsample_frame grows it to the
	 * final resolution and finalize_xyb_color consumes it (after which
	 * save_ref_frame may still take ownership for XYB refs). NULL for
	 * modular frames (they derive ref planes from gmodular directly). */
	microjxl__plane ref_snap[3];
	/* VarDCT pre-filter float pipeline (libjxl's render-pipeline input rows,
	 * float32): the combine writes the IDCT + CfL output here at stored
	 * resolution; finalize_vardct_frame then runs gaborish/EPF FRAME-WIDE
	 * (libjxl's filter stages use symmetric borders across group seams, so
	 * per-group filtering desyncs the output at group boundaries), applies
	 * patches/splines/noise and the colour conversion. Freed after use. */
	microjxl__plane vardct_f[3];
	/* Frame-wide subsampled-chroma staging (raw buffers): the IDCT writes
	 * 8x8 blocks of subsampled channels here at absolute subsampled block
	 * coordinates; finalize_vardct_frame chroma-upsamples them into
	 * vardct_f. Only allocated for non-444 chroma subsampling. */
	float *vardct_ss[3];
	int32_t vardct_ss_w[3], vardct_ss_h[3];
	/* VarDCT render pipeline output (display-referred floats, half
	 * precision — libjxl's pipeline rows are float16): filled by the
	 * non-upsampled combine path or finalize_xyb_color (upsampled path);
	 * consumed (and freed) by the render. Empty for modular frames. */
	microjxl__plane render_f16[3];
	/* Frame-wide EPF state for VarDCT frames: per-8x8 block sharpness and
	 * row_quant (HfMul), gathered from the LF groups during the combine so
	 * microjxl__finalize_vardct_frame can build the whole-frame sigma map
	 * (libjxl's EPF stage reads frame-decoupled sigma rows). */
	int32_t epf_sharp8_w, epf_sharp8_h;
	int32_t *epf_sharp8;
	int32_t *epf_rowq8;
	/* whole-frame reciprocal sigma map (built by finalize_vardct_frame
	 * from the tables above; consumed by microjxl__epf). */
	microjxl__plane epf_sigmas;
	/* Non-XYB modular frames: the restoration-filtered float colour rows
	 * (int/(2^bpp-1) domain). Set by finalize_modular_frame when gaborish/
	 * EPF ran; the render reads these instead of re-quantizing through the
	 * integer channels (libjxl keeps float pipeline rows end to end). */
	microjxl__plane modular_f[3];
	int modular_f_valid;
	/* Per-LF-group dequantized LfQuant planes (post-CfL, PRE-smoothing),
	 * stashed by lf_quant for the frame-wide adaptive DC smoothing: libjxl
	 * runs AdaptiveDCSmoothing on the assembled frame-wide DC image
	 * (dec_frame.cc FinalizeDC, between ProcessDCGroup and ProcessACGroup),
	 * not per LF group. Indexed [gg->idx * 3 + channel]; consumed by
	 * microjxl__smooth_lf_frame (called from microjxl__combine_vardct) and
	 * freed with the frame state. */
	microjxl__plane *lf_stash;
	int lf_stash_have;
} microjxl__frame_st;

MICROJXL_STATIC void microjxl__mem_free_frame_state(microjxl__frame_st *f);
MICROJXL_STATIC void microjxl__mem_free_spline_state(microjxl__frame_st *f);
MICROJXL_STATIC void microjxl__mem_free_patch_dict(microjxl__patch_dict *pd);
MICROJXL__STATIC_RETURNS_ERR microjxl__decode_splines(microjxl__st *st);
MICROJXL__STATIC_RETURNS_ERR microjxl__spline_build_cache(microjxl__st *st);
MICROJXL__STATIC_RETURNS_ERR microjxl__apply_splines_xyb(
	microjxl__st *st, const microjxl__frame_st *f, int32_t gx, int32_t gy, float *x, float *y, float *b);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_STATIC void microjxl__mem_free_frame_state(microjxl__frame_st *f) {
	int32_t i, j, k;
	microjxl__mem_free(f->ec_log_upsampling);
	microjxl__mem_free(f->ec_blend_info);
	microjxl__mem_free(f->name);
	microjxl__mem_free(f->global_tree);
	microjxl__mem_free_code_spec(&f->global_codespec);
	microjxl__mem_free_modular(&f->gmodular);
	microjxl__mem_free(f->block_ctx_map);
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->noise_planes[i]);
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->vardct_f[i]);
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->render_f16[i]);
	for (i = 0; i < MICROJXL__NUM_DCT_PARAMS; ++i) microjxl__mem_free_dq_matrix(&f->dq_matrix[i]);
	for (i = 0; i < MICROJXL__MAX_PASSES; ++i) {
		for (j = 0; j < MICROJXL__NUM_ORDERS; ++j) {
			for (k = 0; k < 3; ++k) {
				microjxl__mem_free(f->orders[i][j][k]);
				f->orders[i][j][k] = NULL;
			}
		}
		microjxl__mem_free_code_spec(&f->coeff_codespec[i]);
	}
	f->ec_log_upsampling = NULL;
	f->ec_blend_info = NULL;
	f->name = NULL;
	f->global_tree = NULL;
	f->block_ctx_map = NULL;
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->noise_planes[i]);
	f->noise_ready = 0;
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->ref_snap[i]);
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->vardct_f[i]);
	for (i = 0; i < 3; ++i) microjxl__mem_free(f->vardct_ss[i]);
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->render_f16[i]);
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->modular_f[i]);
	f->modular_f_valid = 0;
	microjxl__mem_free(f->epf_sharp8);
	microjxl__mem_free(f->epf_rowq8);
	f->epf_sharp8 = NULL;
	f->epf_rowq8 = NULL;
	if (f->lf_stash) {
		for (i = 0; i < (int32_t) f->num_lf_groups * 3; ++i)
			microjxl__mem_free_plane(&f->lf_stash[i]);
		microjxl__mem_free(f->lf_stash);
		f->lf_stash = NULL;
	}
	f->lf_stash_have = 0;
	microjxl__mem_free_plane(&f->epf_sigmas);
	microjxl__mem_free_spline_state(f);
	microjxl__mem_free_patch_dict(&f->patches);
	/* JPEG reconstruction capture buffers (see frame_st jpeg_* fields) */
	for (i = 0; i < 3; ++i) {
		microjxl__mem_free(f->jpeg_coeffs[i]);
		microjxl__mem_free(f->jpeg_dcv[i]);
		f->jpeg_coeffs[i] = NULL;
		f->jpeg_dcv[i] = NULL;
	}
	f->jpeg_recon_alloc = 0;
	f->jpeg_qtable_ok = 0;
	f->jpeg_recon = 0;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// noise (K.5)

#ifdef MICROJXL_IMPLEMENTATION

/* XorShift128+ with SplitMix64 seeding, spec 18181-1 K.5.2 (identical to
 * libjxl xorshift128plus-inl.h). Eight independent lanes are filled in one
 * Fill() call; the 8 resulting 64-bit values are read as 16 sequential
 * 32-bit values per row segment. */
typedef struct microjxl__rng {
	uint64_t s0[8], s1[8];
} microjxl__rng;

static uint64_t microjxl__splitmix64(uint64_t z) {
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

/* libjxl ctor: Xorshift128Plus(seed1, seed2, seed3, seed4) with the call
 * Xorshift128Plus(visible_idx, nonvisible_idx, x0, y0); all four truncated
 * to uint32 there, so the seeds below are the low halves. */
static void microjxl__rng_init(microjxl__rng *r, uint32_t seed1, uint32_t seed2,
		uint32_t seed3, uint32_t seed4) {
	int i;
	r->s0[0] = microjxl__splitmix64(((uint64_t) seed1 << 32) + seed2 + 0x9E3779B97F4A7C15ull);
	r->s1[0] = microjxl__splitmix64(((uint64_t) seed3 << 32) + seed4 + 0x9E3779B97F4A7C15ull);
	for (i = 1; i < 8; ++i) {
		r->s0[i] = microjxl__splitmix64(r->s0[i - 1]);
		r->s1[i] = microjxl__splitmix64(r->s1[i - 1]);
	}
}

static void microjxl__rng_fill(microjxl__rng *r, uint64_t *out /*8 values*/) {
	int i;
	for (i = 0; i < 8; ++i) {
		uint64_t s1 = r->s0[i], s0 = r->s1[i];
		uint64_t bits = s1 + s0;
		r->s0[i] = s0;
		s1 ^= s1 << 23;
		out[i] = bits;
		s1 ^= s0 ^ (s1 >> 18) ^ (s0 >> 5);
		r->s1[i] = s1;
	}
}

/* Generate one full pseudorandom plane of floats in [1,2) (libjxl
 * RandomImage): 16 32-bit random values per batch, each converted with
 * (bits >> 9) | 0x3F800000 reinterpreted as float. Rows top to bottom,
 * segments of up to 16 samples left to right, one Fill() per segment. */
static void microjxl__noise_random_image(microjxl__rng *r, microjxl__plane *p) {
	int32_t x, y, w = p->width, h = p->height;
	for (y = 0; y < h; ++y) {
		float *row = MICROJXL__F32_PIXELS(p, y);
		uint64_t batch[8];
		uint32_t bits[16];
		for (x = 0; x < w; x += 16) {
			int i, n = w - x < 16 ? w - x : 16;
			microjxl__rng_fill(r, batch);
			for (i = 0; i < 8; ++i) {
				bits[i * 2] = (uint32_t) batch[i];
				bits[i * 2 + 1] = (uint32_t) (batch[i] >> 32);
			}
			for (i = 0; i < n; ++i) {
				uint32_t pattern = (bits[i] >> 9) | 0x3F800000u;
				float v;
				memcpy(&v, &pattern, 4);
				row[x + i] = v;
			}
		}
	}
}

/* Mirror-extend coordinate (spec 5.2 Mirror; same rule as microjxl__mirror1d). */
static int32_t microjxl__mirror2(int32_t c, int32_t size) {
	int32_t m = size * 2;
	c %= m;
	if (c < 0) c += m;
	if (c >= size) c = m - 1 - c;
	return c;
}

/* Convolve a float plane in place with the K.5.2 5x5 Laplacian-like kernel
 * (0.16 everywhere except -3.84 at the center), mirror borders. The spec
 * requires the convolution over the channel "in its totality, not in a
 * per-group way". The kernel sums to zero, so [1,2) vs [0,1) input ranges
 * are equivalent. */
static void microjxl__noise_convolve(microjxl__plane *p) {
	int32_t w = p->width, h = p->height, x, y, i, j;
	float *src;
	/* copy the input so borders can reference it */
	src = (float *) malloc(sizeof(float) * (size_t) w * (size_t) h);
	if (!src) return; /* on OOM leave the plane unfiltered: noise degrades, not corrupts */
	for (y = 0; y < h; ++y) memcpy(src + (size_t) y * w, MICROJXL__F32_PIXELS(p, y), sizeof(float) * (size_t) w);
	for (y = 0; y < h; ++y) {
		float *dst = MICROJXL__F32_PIXELS(p, y);
		for (x = 0; x < w; ++x) {
			float acc = -3.84f * src[(size_t) y * w + x];
			for (j = -2; j <= 2; ++j) {
				int32_t yy = microjxl__mirror2(y + j, h);
				for (i = -2; i <= 2; ++i) {
					if (i == 0 && j == 0) continue;
					acc += 0.16f * src[(size_t) yy * w + microjxl__mirror2(x + i, w)];
				}
			}
			dst[x] = acc;
		}
	}
	free(src);
}

/* K.5.2 noise strength: evaluate the 8-point LUT at in_scaled =
 * max(0, in * 6), with linear interpolation and the >=7 clamp to
 * (6, 1.0); the result is additionally clamped to [0, 1]. A negative
 * input is clamped to zero (libjxl Max(Zero(), x*6)), which evaluates
 * the LUT at 0 -- NOT a zero strength. */
static float microjxl__noise_strength_lut(const float *lut, float in) {
	float scaled = in * 6.0f, fr;
	int32_t fl;
	if (scaled < 0.0f) scaled = 0.0f;
	if (scaled >= 7.0f) {
		fl = 6;
		fr = 1.0f;
	} else {
		fl = (int32_t) scaled;
		fr = scaled - (float) fl;
	}
	/* fl <= 6 so fl+1 <= 7 is in range (kNumNoisePoints = 8) */
	{
		float s = lut[fl] * (1.0f - fr) + lut[fl + 1] * fr;
		return s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
	}
}

/* K.5.2: generate and convolve the three frame-sized pseudorandom planes
 * into f->noise_planes. Called once per frame that uses noise: from
 * combine_vardct (VarDCT; stored-resolution planes, matching the stored
 * coordinates the combine works in) or from the renderer (modular; final
 * resolution after upsampling, matching libjxl's post-upsampling noise
 * stage). The LUT itself was decoded in LfGlobal. */
MICROJXL__STATIC_RETURNS_ERR microjxl__add_noise_frame(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t i, gs, ntx, nty, tx, ty;

	if (!f->has_noise || f->noise_ready) return 0;

	/* K.5.2 frame indices: vis_frame_idx counts visible frames decoded so
	 * far (kRegularFrame/kSkipProgressive with duration > 0 or is_last);
	 * nonvis_frame_idx counts invisible frames since the previous visible
	 * one. The current frame's increment is applied by the frame loop
	 * (microjxl__advance) right after its header is parsed, matching libjxl's
	 * InitFrame ordering (dec_frame.cc). */
	{
		int64_t visible_idx = im->vis_frame_idx, nonvisible_idx = im->nonvis_frame_idx;

		gs = 1 << f->group_size_shift;
		for (i = 0; i < 3; ++i) {
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &f->noise_planes[i]));
		}
		/* One seeded generator per group_dim tile of the frame (the seed
		 * carries the tile's top-left coordinate); each tile fills its
		 * three planes in R, G, correlated order with the generator state
		 * carried across the planes (libjxl Random3Planes). Edge tiles are
		 * generated clipped to the frame, exactly like libjxl's Rect.
		 * Cross-tile borders are handled by the whole-plane convolution
		 * below, so tiles do not need overhang pixels. */
		ntx = microjxl__ceil_div32(f->width, gs);
		nty = microjxl__ceil_div32(f->height, gs);
		for (ty = 0; ty < nty; ++ty) for (tx = 0; tx < ntx; ++tx) {
			int32_t x0 = tx * gs, y0 = ty * gs;
			int32_t tw = microjxl__min32(f->width - x0, gs), th = microjxl__min32(f->height - y0, gs);
			microjxl__rng rng;
			microjxl__rng_init(&rng, (uint32_t) visible_idx, (uint32_t) nonvisible_idx,
				(uint32_t) x0, (uint32_t) y0);
			for (i = 0; i < 3; ++i) {
				microjxl__plane t = MICROJXL__INIT;
				int32_t yy;
				MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, tw, th, 0, &t));
				microjxl__noise_random_image(&rng, &t);
				for (yy = 0; yy < th; ++yy) {
					memcpy(MICROJXL__F32_PIXELS(&f->noise_planes[i], y0 + yy) + x0,
						MICROJXL__F32_PIXELS(&t, yy), sizeof(float) * (size_t) tw);
				}
				microjxl__mem_free_plane(&t);
			}
		}
	}
	for (i = 0; i < 3; ++i) microjxl__noise_convolve(&f->noise_planes[i]);
	f->noise_ready = 1;
	return 0;

MICROJXL__ON_ERROR:
	for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&f->noise_planes[i]);
	f->noise_ready = 0;
	return st->err;
}

/* K.5.2 modulate equations for one pixel. x/y/b point at the XYB float
 * samples (pre-colour-transform); gx/gy are the pixel's frame coordinates
 * into the convolved random planes. NR mixes the red and correlated
 * planes with the 1/128 + 127/128 weights; X/Y/B then take the
 * base-correlation terms from LfChannelCorrelation. */
static void microjxl__apply_noise_xyb(const microjxl__frame_st *f, const microjxl__plane *np,
		int32_t gx, int32_t gy, float *x, float *y, float *b) {
	const float *lut = f->noise_lut;
	float sR = microjxl__noise_strength_lut(lut, (*y + *x) * 0.5f);
	float sG = microjxl__noise_strength_lut(lut, (*y - *x) * 0.5f);
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_NOISE")) {
		static int32_t tr_x = -1, tr_y = -1;
		const char *tp = getenv("MICROJXL_TRACE_NOISE_AT");
		if (tp && tr_x < 0) { sscanf(tp, "%d,%d", &tr_x, &tr_y); }
		if (gx == tr_x && gy == tr_y) {
			fprintf(stderr, "[mj-noise] (%d,%d) pre x=%.9g y=%.9g b=%.9g sR=%.9g sG=%.9g rnd=%.9g,%.9g,%.9g\n",
				gx, gy, (double) *x, (double) *y, (double) *b, (double) sR, (double) sG,
				(double) MICROJXL__F32_PIXELS(&np[0], gy)[gx], (double) MICROJXL__F32_PIXELS(&np[1], gy)[gx],
				(double) MICROJXL__F32_PIXELS(&np[2], gy)[gx]);
		}
	}
#endif
	float aR = MICROJXL__F32_PIXELS(&np[0], gy)[gx] * 0.22f;
	float aG = MICROJXL__F32_PIXELS(&np[1], gy)[gx] * 0.22f;
	float aC = MICROJXL__F32_PIXELS(&np[2], gy)[gx] * 0.22f;
	float nr = (1.0f / 128.0f * aR + 127.0f / 128.0f * aC) * sR;
	float ng = (1.0f / 128.0f * aG + 127.0f / 128.0f * aC) * sG;
	*x += f->base_corr_x * (nr + ng) + nr - ng;
	*y += nr + ng;
	*b += f->base_corr_b * (nr + ng);
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// splines (K.4)

#ifdef MICROJXL_IMPLEMENTATION

/* scalar fast-math, exact ports of libjxl fast_math-inl.h (the spline draw
 * math must match libjxl bit-for-bit closely enough for 8-bit output) */

static float microjxl__spline_fastcosf(float x) {
	const float kPi_ = 3.14159265358979323846f;
	float pi2 = kPi_ * 2.0f;
	float npi2 = floorf(x * (0.5f / kPi_)) * pi2;
	float xmod = x - npi2;
	float x_pi = microjxl__minf(xmod, pi2 - xmod);
	int above_pihalf = x_pi >= kPi_ * 0.5f;
	float x_pihalf = above_pihalf ? kPi_ - x_pi : x_pi;
	float xs = x_pihalf * 0.25f;
	float x2 = xs * xs, x4 = x2 * x2;
	float pre = x4 * 0.06960438f + (x2 * -0.84087373f + 1.68179268f);
	float s1 = pre * pre - 1.414213562f;
	float s2 = s1 * s1 - 1.0f;
	return above_pihalf ? -s2 : s2;
}

static float microjxl__spline_fasterff(float x) {
	int xle0 = x <= 0.0f;
	float absx = fabsf(x);
	float d1 = absx * 7.77394369e-02f + 2.05260015e-04f;
	float d2 = d1 * absx + 2.32120216e-01f;
	float d3 = d2 * absx + 2.77820801e-01f;
	float d4 = d3 * absx + 1.0f;
	float d5 = d4 * d4;
	float inv = 1.0f / d5;
	float result = 1.0f - inv * inv;
	return xle0 ? -result : result;
}

/* ContinuousIDCT (libjxl splines.cc): DCT-3 of 32 coefficients rescaled by
 * kSqrt2, evaluated at t+0.5 with the fast cosine. */
static float microjxl__spline_continuous_idct(const float *dct, float t) {
	static const float MULTIPLIERS[32] = {
		3.14159265358979323846f / 32 * 0,  3.14159265358979323846f / 32 * 1,
		3.14159265358979323846f / 32 * 2,  3.14159265358979323846f / 32 * 3,
		3.14159265358979323846f / 32 * 4,  3.14159265358979323846f / 32 * 5,
		3.14159265358979323846f / 32 * 6,  3.14159265358979323846f / 32 * 7,
		3.14159265358979323846f / 32 * 8,  3.14159265358979323846f / 32 * 9,
		3.14159265358979323846f / 32 * 10, 3.14159265358979323846f / 32 * 11,
		3.14159265358979323846f / 32 * 12, 3.14159265358979323846f / 32 * 13,
		3.14159265358979323846f / 32 * 14, 3.14159265358979323846f / 32 * 15,
		3.14159265358979323846f / 32 * 16, 3.14159265358979323846f / 32 * 17,
		3.14159265358979323846f / 32 * 18, 3.14159265358979323846f / 32 * 19,
		3.14159265358979323846f / 32 * 20, 3.14159265358979323846f / 32 * 21,
		3.14159265358979323846f / 32 * 22, 3.14159265358979323846f / 32 * 23,
		3.14159265358979323846f / 32 * 24, 3.14159265358979323846f / 32 * 25,
		3.14159265358979323846f / 32 * 26, 3.14159265358979323846f / 32 * 27,
		3.14159265358979323846f / 32 * 28, 3.14159265358979323846f / 32 * 29,
		3.14159265358979323846f / 32 * 30, 3.14159265358979323846f / 32 * 31,
	};
	static const float KSQRT2 = 1.41421356237309504880f;
	float result = 0.0f;
	int i;
	for (i = 0; i < 32; ++i) {
		result += dct[i] * microjxl__spline_fastcosf(MULTIPLIERS[i] * (t + 0.5f)) * KSQRT2;
	}
	return result;
}

static void microjxl__mem_free_spline_state(microjxl__frame_st *f) {
	int32_t i;
	if (f->splines) {
		for (i = 0; i < f->num_splines; ++i) microjxl__mem_free(f->splines[i].points);
	}
	microjxl__mem_free(f->splines);
	microjxl__mem_free(f->starting_points);
	microjxl__mem_free(f->segments);
	microjxl__mem_free(f->segment_indices);
	microjxl__mem_free(f->segment_y_start);
	f->splines = NULL;
	f->starting_points = NULL;
	f->segments = NULL;
	f->segment_indices = NULL;
	f->segment_y_start = NULL;
	f->num_splines = 0;
	f->num_segments = 0;
	f->spline_cache_h = 0;
}

/* K.4.1: the spline bundle in LfGlobal (before the noise LUT in the bundle
 * order; libjxl dec_frame.cc calls Splines::Decode with the STORED frame
 * dimensions for the control-point limit). Reads a standalone ANS stream
 * with 6 contexts; no MA tree, so all symbols come from the same clusters
 * selected by the context map. dist_mult = 0 matches libjxl's plain
 * ReadHybridUint (distance multiplier 1). */
MICROJXL__STATIC_RETURNS_ERR microjxl__decode_splines(microjxl__st *st) {
	microjxl__frame_st *f = st->frame;
	microjxl__code_spec codespec = MICROJXL__INIT;
	microjxl__code_st code = MICROJXL__INIT;
	int32_t num_splines, max_control_points, total_cp;
	int32_t i, k, c;
	int64_t num_pixels = (int64_t) f->width * (int64_t) f->height;

	/* re-entry safety: LfGlobal re-runs from the top when the source buffer
	 * is exhausted mid-section, so partial state must be released first */
	microjxl__mem_free_spline_state(f);

	MICROJXL__TRY(microjxl__read_code_spec(st, 6, &codespec));
	microjxl__init_code(&code, &codespec);

	num_splines = microjxl__code(st, 2, 0, &code);
	MICROJXL__RAISE_DELAYED();
	max_control_points = num_pixels / 2;
	if (max_control_points > (1 << 20)) max_control_points = 1 << 20;
	MICROJXL__SHOULD(num_splines <= max_control_points, "spln");
	num_splines++;
	MICROJXL__SHOULD(num_splines <= max_control_points, "spln");
	MICROJXL__SHOULD(num_splines >= 0, "spln");

	/* starting points: context 1, cumulative unpacked, limit 2^23 exclusive;
	 * the first point is raw (not offset by a previous point) */
	MICROJXL__TRY_CALLOC(microjxl__spline_pt, &f->starting_points, (size_t) num_splines);
	{
		int64_t last_x = 0, last_y = 0;
		for (i = 0; i < num_splines; ++i) {
			int64_t dx, dy, x, y;
			uint32_t ux = (uint32_t) microjxl__code(st, 1, 0, &code);
			MICROJXL__RAISE_DELAYED();
			uint32_t uy = (uint32_t) microjxl__code(st, 1, 0, &code);
			MICROJXL__RAISE_DELAYED();
			dx = 0; dy = 0;
			if (i != 0) {
				x = microjxl__unpack_signed64((int64_t) ux) + last_x;
				y = microjxl__unpack_signed64((int64_t) uy) + last_y;
			} else {
				x = (int64_t) ux;
				y = (int64_t) uy;
			}
			MICROJXL__SHOULD(x > -(1 << 23) && x < (1 << 23), "spln");
			MICROJXL__SHOULD(y > -(1 << 23) && y < (1 << 23), "spln");
			f->starting_points[i].x = (float) x;
			f->starting_points[i].y = (float) y;
			last_x = x;
			last_y = y;
		}
	}

	f->spline_qadjust = microjxl__unpack_signed(microjxl__code(st, 0, 0, &code));
	MICROJXL__RAISE_DELAYED();

	f->num_splines = num_splines;
	MICROJXL__TRY_CALLOC(microjxl__spline_q, &f->splines, (size_t) num_splines);
	total_cp = num_splines;
	for (i = 0; i < num_splines; ++i) {
		microjxl__spline_q *s = &f->splines[i];
		int32_t ncp = microjxl__code(st, 3, 0, &code);
		MICROJXL__RAISE_DELAYED();
		MICROJXL__SHOULD(ncp >= 0 && ncp <= max_control_points, "spln");
		total_cp += ncp;
		MICROJXL__SHOULD(total_cp <= max_control_points, "spln");
		s->num_points = ncp;
		MICROJXL__TRY_CALLOC(int64_t, &s->points, (size_t) ncp * 2 + 1);
		for (k = 0; k < ncp; ++k) {
			int64_t dx = microjxl__unpack_signed64((int64_t) (uint32_t) microjxl__code(st, 4, 0, &code));
			MICROJXL__RAISE_DELAYED();
			int64_t dy = microjxl__unpack_signed64((int64_t) (uint32_t) microjxl__code(st, 4, 0, &code));
			MICROJXL__RAISE_DELAYED();
			MICROJXL__SHOULD(dx > -(1 << 30) && dx < (1 << 30), "spln");
			MICROJXL__SHOULD(dy > -(1 << 30) && dy < (1 << 30), "spln");
			s->points[k * 2] = dx;
			s->points[k * 2 + 1] = dy;
		}
		for (c = 0; c < 4; ++c) {
			for (k = 0; k < 32; ++k) {
				int64_t v = microjxl__unpack_signed64((int64_t) (uint32_t) microjxl__code(st, 5, 0, &code));
				MICROJXL__RAISE_DELAYED();
				MICROJXL__SHOULD(v != INT32_MIN, "spln"); // libjxl rejects the weird number
				s->dct[c][k] = (int32_t) v;
			}
		}
	}

	MICROJXL__TRY(microjxl__finish_and_free_code(st, &code));
	microjxl__mem_free_code_spec(&codespec);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_code(&code);
	microjxl__mem_free_code_spec(&codespec);
	microjxl__mem_free_spline_state(f);
	return st->err;
}

/* K.4.2 dequantization (libjxl QuantizedSpline::Dequantize): rebuilds the
 * absolute control points from the delta-deltas with the area limits, and
 * dequantizes the DCTs with the channel weights and the base colour
 * correlations (ytox/ytob = cmap base = j40's original base_corr_x/base_corr_b). */
static int microjxl__spline_dequant(
	microjxl__st *st, const microjxl__frame_st *f, const microjxl__spline_q *s, const microjxl__spline_pt *start,
	int64_t area_limit, uint64_t *total_area, microjxl__spline_pt **out_points, int32_t *out_npoints,
	float out_cdct[3][32], float out_sdct[32]
) {
	static const float KSQRT0_5 = 0.70710678118654752440f;
	static const float KW[4] = {0.0042f, 0.075f, 0.07f, 0.3333f};
	float inv_quant;
	float px, py;
	int32_t cur_x, cur_y, cdx = 0, cdy = 0;
	uint64_t manhattan = 0;
	int32_t i, c, k;
	uint64_t color[3] = {0, 0, 0}, max_color, logcolor, width_estimate = 0;
	float weight_limit;
	microjxl__spline_pt *pts;

	inv_quant = f->spline_qadjust >= 0 ?
		1.0f / (1.0f + 0.125f * (float) f->spline_qadjust) :
		1.0f - 0.125f * (float) f->spline_qadjust;

	px = roundf(start->x);
	py = roundf(start->y);
	MICROJXL__SHOULD(px > -(1 << 23) && px < (1 << 23), "spln");
	MICROJXL__SHOULD(py > -(1 << 23) && py < (1 << 23), "spln");
	cur_x = (int32_t) px;
	cur_y = (int32_t) py;

	*out_npoints = s->num_points + 1;
	pts = (microjxl__spline_pt *) microjxl__malloc((size_t) *out_npoints, sizeof(microjxl__spline_pt));
	MICROJXL__SHOULD(pts, "!mem");
	pts[0].x = (float) cur_x;
	pts[0].y = (float) cur_y;
	for (i = 0; i < s->num_points; ++i) {
		cdx += (int32_t) s->points[i * 2];
		cdy += (int32_t) s->points[i * 2 + 1];
		manhattan += (uint64_t) (microjxl__abs32(cdx) + microjxl__abs32(cdy));
		MICROJXL__SHOULD(manhattan <= (uint64_t) area_limit, "spln");
		MICROJXL__SHOULD(cdx > -(1 << 23) && cdx < (1 << 23), "spln");
		MICROJXL__SHOULD(cdy > -(1 << 23) && cdy < (1 << 23), "spln");
		cur_x += cdx;
		cur_y += cdy;
		MICROJXL__SHOULD(cur_x > -(1 << 23) && cur_x < (1 << 23), "spln");
		MICROJXL__SHOULD(cur_y > -(1 << 23) && cur_y < (1 << 23), "spln");
		pts[i + 1].x = (float) cur_x;
		pts[i + 1].y = (float) cur_y;
	}

	for (c = 0; c < 3; ++c) {
		for (i = 0; i < 32; ++i) {
			float idf = (i == 0) ? KSQRT0_5 : 1.0f;
			out_cdct[c][i] = (float) s->dct[c][i] * idf * KW[c] * inv_quant;
		}
	}
	for (i = 0; i < 32; ++i) {
		out_cdct[0][i] += f->base_corr_x * out_cdct[1][i];
		out_cdct[2][i] += f->base_corr_b * out_cdct[1][i];
	}

	/* area estimate (libjxl): colors are ceil(inv_quant * |quantized dct|) */
	for (c = 0; c < 3; ++c) {
		for (i = 0; i < 32; ++i) {
			float v = inv_quant * (float) microjxl__abs32(s->dct[c][i]);
			color[c] += (uint64_t) ceilf(v);
		}
	}
	color[0] += (uint64_t) ceilf(fabsf(f->base_corr_x)) * color[1];
	color[2] += (uint64_t) ceilf(fabsf(f->base_corr_b)) * color[1];
	max_color = color[0] > color[1] ? color[0] : color[1];
	if (color[2] > max_color) max_color = color[2];
	logcolor = 1;
	if (1 + max_color > 1) {
		/* CeilLog2Nonzero(1 + max_color) */
		uint64_t v = 1 + max_color;
		int lg = 0;
		while ((v >> lg) > 1) ++lg;
		if ((v & (v - 1)) != 0) ++lg;
		logcolor = (uint64_t) (lg > 0 ? lg : 1);
	}
	weight_limit = ceilf(sqrtf(
		((float) area_limit / (float) logcolor) / (float) (manhattan ? manhattan : 1)));
	for (i = 0; i < 32; ++i) {
		float idf = (i == 0) ? KSQRT0_5 : 1.0f;
		float wf;
		uint64_t weight;
		out_sdct[i] = (float) s->dct[3][i] * idf * KW[3] * inv_quant;
		wf = ceilf(inv_quant * (float) microjxl__abs32(s->dct[3][i]));
		if (wf < 1.0f) wf = 1.0f;
		if (wf > weight_limit) wf = weight_limit;
		weight = (uint64_t) wf;
		width_estimate += weight * weight * logcolor;
	}
	*total_area += width_estimate * manhattan;
	MICROJXL__SHOULD(*total_area <= (uint64_t) area_limit, "spln");

	*out_points = pts;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(pts);
	return st->err;
}

/* centripetal Catmull-Rom (16 subdivisions per segment) + equally spaced
 * resampling (libjxl DrawCentripetalCatmullRomSpline +
 * ForEachEquallySpacedPoint). The final emitted point carries the leftover
 * arc distance as its multiplier; all interior multipliers are 1. */
static int microjxl__spline_draw_points(
	microjxl__st *st, const microjxl__spline_pt *ctrl, int32_t nctrl,
	microjxl__spline_pt **out_draw, float **out_mults, int32_t *out_ndraw, float *out_arc
) {
	microjxl__spline_pt *pts = NULL, *interp = NULL, *draw = NULL;
	float *mults = NULL;
	int32_t npts, ninterp, ndraw = 0, cap;
	int32_t i, k;

	*out_draw = NULL;
	*out_mults = NULL;
	*out_ndraw = 0;
	*out_arc = 0.0f;
	if (nctrl == 1) return 0; /* arc_length = 0, the spline has no effect */

	/* extended points: front p0 + (p0 - p1), back pn + (pn - pn-1) */
	npts = nctrl + 2;
	pts = (microjxl__spline_pt *) microjxl__malloc((size_t) npts, sizeof(microjxl__spline_pt));
	MICROJXL__SHOULD(pts, "!mem");
	pts[0].x = ctrl[0].x + (ctrl[0].x - ctrl[1].x);
	pts[0].y = ctrl[0].y + (ctrl[0].y - ctrl[1].y);
	memcpy(&pts[1], ctrl, (size_t) nctrl * sizeof(microjxl__spline_pt));
	pts[npts - 1].x = ctrl[nctrl - 1].x + (ctrl[nctrl - 1].x - ctrl[nctrl - 2].x);
	pts[npts - 1].y = ctrl[nctrl - 1].y + (ctrl[nctrl - 1].y - ctrl[nctrl - 2].y);

	ninterp = (npts - 3) * 16 + 1;
	interp = (microjxl__spline_pt *) microjxl__malloc((size_t) ninterp, sizeof(microjxl__spline_pt));
	MICROJXL__SHOULD(interp, "!mem");
	{
		int32_t start, sub, out = 0;
		for (start = 0; start < npts - 3; ++start) {
			const microjxl__spline_pt *p = &pts[start];
			float d[3], t[4];
			t[0] = 0.0f;
			for (sub = 0; sub < 3; ++sub) {
				d[sub] = sqrtf(hypotf(p[sub + 1].x - p[sub].x, p[sub + 1].y - p[sub].y));
				t[sub + 1] = t[sub] + d[sub];
			}
			interp[out++] = p[1];
			for (sub = 1; sub < 16; ++sub) {
				float tt = d[0] + ((float) sub / 16.0f) * d[1];
				microjxl__spline_pt a[3], b[2];
				for (k = 0; k < 3; ++k) {
					a[k].x = p[k].x + ((tt - t[k]) / d[k]) * (p[k + 1].x - p[k].x);
					a[k].y = p[k].y + ((tt - t[k]) / d[k]) * (p[k + 1].y - p[k].y);
				}
				for (k = 0; k < 2; ++k) {
					b[k].x = a[k].x + ((tt - t[k]) / (d[k] + d[k + 1])) * (a[k + 1].x - a[k].x);
					b[k].y = a[k].y + ((tt - t[k]) / (d[k] + d[k + 1])) * (a[k + 1].y - a[k].y);
				}
				interp[out].x = b[0].x + ((tt - t[1]) / d[1]) * (b[1].x - b[0].x);
				interp[out].y = b[0].y + ((tt - t[1]) / d[1]) * (b[1].y - b[0].y);
				++out;
			}
		}
		interp[out++] = pts[npts - 2];
		MICROJXL__ASSERT(out == ninterp);
	}

	/* equally spaced walk */
	cap = ninterp + ninterp / 4 + 16;
	draw = (microjxl__spline_pt *) microjxl__malloc((size_t) cap, sizeof(microjxl__spline_pt));
	MICROJXL__SHOULD(draw, "!mem");
	mults = (float *) microjxl__malloc((size_t) cap, sizeof(float));
	MICROJXL__SHOULD(mults, "!mem");
	{
		microjxl__spline_pt current = interp[0];
		int32_t next = 0;
		/* the interpolation step does not advance `next`, so a spline whose
		 * total arc length exceeds ninterp emits more points than ninterp+O(1);
		 * grow the buffers as needed */
#define MICROJXL__SPLINE_GROW_DRAW() \
		do { \
			if (ndraw == cap) { \
				microjxl__spline_pt *ndrawbuf; float *nmults; \
				cap *= 2; \
				ndrawbuf = (microjxl__spline_pt *) MICROJXL_REALLOC(draw, (size_t) cap * sizeof(microjxl__spline_pt)); \
				MICROJXL__SHOULD(ndrawbuf, "!mem"); \
				draw = ndrawbuf; \
				nmults = (float *) MICROJXL_REALLOC(mults, (size_t) cap * sizeof(float)); \
				MICROJXL__SHOULD(nmults, "!mem"); \
				mults = nmults; \
			} \
		} while (0)
		draw[ndraw] = current;
		mults[ndraw] = 1.0f;
		++ndraw;
		while (next < ninterp) {
			microjxl__spline_pt prev = current;
			float arcprev = 0.0f;
			for (;;) {
				float arc;
				if (next >= ninterp) {
					MICROJXL__SPLINE_GROW_DRAW();
					draw[ndraw] = prev;
					mults[ndraw] = arcprev;
					++ndraw;
					goto walk_done;
				}
				arc = sqrtf((interp[next].x - prev.x) * (interp[next].x - prev.x) +
					(interp[next].y - prev.y) * (interp[next].y - prev.y));
				if (arcprev + arc >= 1.0f) {
					float frac = (1.0f - arcprev) / arc;
					current.x = prev.x + frac * (interp[next].x - prev.x);
					current.y = prev.y + frac * (interp[next].y - prev.y);
					MICROJXL__SPLINE_GROW_DRAW();
					draw[ndraw] = current;
					mults[ndraw] = 1.0f;
					++ndraw;
					break;
				}
				arcprev += arc;
				prev = interp[next];
				++next;
			}
		}
	walk_done:;
#undef MICROJXL__SPLINE_GROW_DRAW
	}

	{
		float arc = (float) (ndraw - 2) + mults[ndraw - 1];
		if (arc <= 0.0f) {
			microjxl__mem_free(pts);
			microjxl__mem_free(interp);
			microjxl__mem_free(draw);
			microjxl__mem_free(mults);
			return 0; /* this spline would not have any effect */
		}
		*out_arc = arc;
	}
	*out_draw = draw;
	*out_mults = mults;
	*out_ndraw = ndraw;
	microjxl__mem_free(pts);
	microjxl__mem_free(interp);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(pts);
	microjxl__mem_free(interp);
	microjxl__mem_free(draw);
	microjxl__mem_free(mults);
	return st->err;
}

/* SegmentsFromPoints + ComputeSegments (libjxl): one Gaussian per equally
 * spaced point, appended to the caller's segment/span arrays. */
static int microjxl__spline_make_segments(
	microjxl__st *st, int32_t image_ysize,
	const microjxl__spline_pt *draw, const float *mults, int32_t ndraw, float arc_length,
	const float cdct[3][32], const float sdct[32],
	microjxl__spline_seg **segs, int32_t *seg_cap, int32_t *num_segs,
	int32_t **spans_y0, int32_t *cap0, int32_t **spans_y1, int32_t *cap1
) {
	static const float K_DISTANCE_EXP = 5.0f; /* JXL_HIGH_PRECISION = 1 */
	float inv_arc = 1.0f / arc_length;
	int32_t i, c;

	for (i = 0; i < ndraw; ++i) {
		float progress = (float) i * inv_arc;
		float color[3], sigma, intensity, max_color, maxdist;
		int64_t y0, y1;
		microjxl__spline_seg *seg;

		if (progress > 1.0f) progress = 1.0f;
		for (c = 0; c < 3; ++c) color[c] = microjxl__spline_continuous_idct(cdct[c], 31.0f * progress);
		sigma = microjxl__spline_continuous_idct(sdct, 31.0f * progress);
		intensity = mults[i];

		if (!(isfinite(sigma) && sigma != 0.0f && isfinite(1.0f / sigma) &&
			isfinite(intensity))) continue;
		max_color = 0.01f;
		for (c = 0; c < 3; ++c) max_color = microjxl__maxf(max_color, fabsf(color[c] * intensity));
		maxdist = sqrtf(-2.0f * sigma * sigma *
			(logf(0.1f) * K_DISTANCE_EXP - logf(max_color)));

		y0 = (int64_t) llroundf(draw[i].y - maxdist);
		if (y0 < 0) y0 = 0;
		y1 = (int64_t) llroundf(draw[i].y + maxdist) + 1;
		if (y1 > image_ysize) y1 = image_ysize;
		if (y1 <= y0) continue;

		MICROJXL__SHOULD(*num_segs < INT32_MAX / 2, "spln");
		MICROJXL__TRY_REALLOC32(microjxl__spline_seg, segs, *num_segs + 1, seg_cap);
		MICROJXL__TRY_REALLOC32(int32_t, spans_y0, *num_segs + 1, cap0);
		MICROJXL__TRY_REALLOC32(int32_t, spans_y1, *num_segs + 1, cap1);

		seg = &(*segs)[*num_segs];
		seg->center_x = draw[i].x;
		seg->center_y = draw[i].y;
		for (c = 0; c < 3; ++c) seg->color[c] = color[c];
		seg->inv_sigma = 1.0f / sigma;
		seg->sigma_over_4_times_intensity = 0.25f * sigma * intensity;
		seg->maximum_distance = maxdist;
		(*spans_y0)[*num_segs] = (int32_t) y0;
		(*spans_y1)[*num_segs] = (int32_t) y1;
		++*num_segs;
	}
	return 0;

MICROJXL__ON_ERROR:
	return st->err;
}

/* K.4.2: build the draw cache. Dequantization uses the base colour
 * correlations (LfChannelCorrelation is parsed before this call, defaults
 * 0 / 1.0 for modular frames), and the per-row Gaussian segments are built
 * against the UPSAMPLED frame dimensions (libjxl dec_frame.cc:305) while
 * the draw stage itself runs at stored resolution before upsampling. */
MICROJXL__STATIC_RETURNS_ERR microjxl__spline_build_cache(microjxl__st *st) {
	microjxl__frame_st *f = st->frame;
	microjxl__spline_pt *ctrl = NULL, *draw = NULL;
	float *mults = NULL;
	int32_t *spans_y0 = NULL, *spans_y1 = NULL;
	int32_t ctrl_cap = 0, seg_cap = 0, cap0 = 0, cap1 = 0;
	int32_t uph = f->upsampled_height;
	int64_t image_size = (int64_t) f->upsampled_width * (int64_t) uph;
	int64_t area_limit = image_size < (1LL << 32) ?
		1024 * image_size + (1LL << 32) : (1LL << 42);
	uint64_t total_area = 0;
	int32_t i, j;

	f->num_segments = 0;
	f->segments = NULL;
	f->segment_indices = NULL;
	f->segment_y_start = NULL;
	f->spline_cache_h = 0;

	for (i = 0; i < f->num_splines; ++i) {
		microjxl__spline_q *s = &f->splines[i];
		float cdct[3][32], sdct[32];
		int32_t nctrl, ndraw;
		float arc;

		MICROJXL__TRY(microjxl__spline_dequant(st, f, s, &f->starting_points[i],
			area_limit, &total_area, &ctrl, &nctrl, cdct, sdct));
		/* identical successive control points would break the centripetal
		 * parameterization (libjxl fails in that case as well) */
		for (j = 1; j < nctrl; ++j) {
			MICROJXL__SHOULD(!(fabsf(ctrl[j - 1].x - ctrl[j].x) < 1e-3f &&
				fabsf(ctrl[j - 1].y - ctrl[j].y) < 1e-3f), "spln");
		}
		MICROJXL__TRY(microjxl__spline_draw_points(st, ctrl, nctrl, &draw, &mults, &ndraw, &arc));
		microjxl__mem_free(ctrl);
		ctrl = NULL;
		if (draw) {
			MICROJXL__TRY(microjxl__spline_make_segments(st, uph, draw, mults, ndraw, arc,
				cdct, sdct, &f->segments, &seg_cap, &f->num_segments,
				&spans_y0, &cap0, &spans_y1, &cap1));
			microjxl__mem_free(draw);
			microjxl__mem_free(mults);
			draw = NULL;
			mults = NULL;
		}
	}

	/* population pass: row y -> [segment_y_start[y], segment_y_start[y+1])
	 * ranges into segment_indices (libjxl InitializeDrawCache) */
	f->spline_cache_h = uph;
	MICROJXL__TRY_MALLOC(int32_t, &f->segment_y_start, (size_t) (uph + 2));
	memset(f->segment_y_start, 0, sizeof(int32_t) * (size_t) (uph + 2));
	{
		int32_t *population = f->segment_y_start + 1;
		int64_t total = 0, coverage = 0;
		int32_t y;
		for (i = 0; i < f->num_segments; ++i) {
			++population[spans_y0[i]];
			--population[spans_y1[i]];
		}
		for (y = 0; y < uph; ++y) {
			coverage += population[y];
			population[y] = (int32_t) total;
			total += coverage;
		}
		MICROJXL__SHOULD(total <= INT32_MAX, "spln");
		if (total > 0) {
			MICROJXL__TRY_MALLOC(int32_t, &f->segment_indices, (size_t) total);
			for (i = 0; i < f->num_segments; ++i) {
				for (y = spans_y0[i]; y < spans_y1[i]; ++y)
					f->segment_indices[population[y]++] = i;
			}
		}
	}

	microjxl__mem_free(spans_y0);
	microjxl__mem_free(spans_y1);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(ctrl);
	microjxl__mem_free(draw);
	microjxl__mem_free(mults);
	microjxl__mem_free(spans_y0);
	microjxl__mem_free(spans_y1);
	return st->err;
}

/* per-pixel spline deltas at frame pixel (gx, gy) in stored-resolution
 * coordinates: the libjxl spline render stage runs before frame upsampling
 * (stage order: splines -> upsampling -> noise). The span test uses the
 * same llround bounds as libjxl's DrawSegment, with `end` inclusive. */
static void microjxl__spline_deltas_at(const microjxl__frame_st *f, int32_t gx, int32_t gy, float d[3]) {
	int32_t i, s, e;
	d[0] = d[1] = d[2] = 0.0f;
	if (!f->num_segments || gy < 0 || gy >= f->spline_cache_h) return;
	s = f->segment_y_start[gy];
	e = f->segment_y_start[gy + 1];
	for (i = s; i < e; ++i) {
		const microjxl__spline_seg *seg = &f->segments[f->segment_indices[i]];
		int64_t xs, xe;
		float dx, dy, dist, factor, inten;
		xs = llroundf(seg->center_x - seg->maximum_distance);
		xe = llroundf(seg->center_x + seg->maximum_distance);
		if (gx < xs || gx > xe) continue;
		dx = (float) gx - seg->center_x;
		dy = (float) gy - seg->center_y;
		dist = sqrtf(dx * dx + dy * dy);
		factor = microjxl__spline_fasterff((dist * 0.5f + 0.353553391f) * seg->inv_sigma)
			- microjxl__spline_fasterff((dist * 0.5f - 0.353553391f) * seg->inv_sigma);
		inten = seg->sigma_over_4_times_intensity * factor * factor;
		d[0] += seg->color[0] * inten;
		d[1] += seg->color[1] * inten;
		d[2] += seg->color[2] * inten;
	}
}

MICROJXL__STATIC_RETURNS_ERR microjxl__apply_splines_xyb(
	microjxl__st *st, const microjxl__frame_st *f, int32_t gx, int32_t gy, float *x, float *y, float *b
) {
	float d[3];
	(void) st;
	microjxl__spline_deltas_at(f, gx, gy, d);
	*x += d[0];
	*y += d[1];
	*b += d[2];
	return 0;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// frame header

MICROJXL__STATIC_RETURNS_ERR microjxl__frame_header(microjxl__st *st);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__frame_header(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t i, j;

	f->is_last = 1;
	f->type = MICROJXL__FRAME_REGULAR;
	f->is_modular = 0;
	f->has_noise = f->has_patches = f->has_splines = f->use_lf_frame = f->skip_adapt_lf_smooth = 0;
	f->do_ycbcr = 0;
	f->jpeg_upsampling = 0;
	f->log_upsampling = 0;
	f->ec_log_upsampling = NULL;
	f->group_size_shift = 8;
	f->x_qm_scale = 3;
	f->b_qm_scale = 2;
	f->num_passes = 1;
	f->shift[0] = 0; // last pass if default
	f->log_ds[0] = 3; f->log_ds[1] = 0; // last pass if default
	f->num_ds = 0;
	f->lf_level = 0;
	f->x0 = f->y0 = 0;
	f->width = im->width;
	f->height = im->height;
	f->duration = f->timecode = 0;
	f->blend_info.mode = MICROJXL__BLEND_REPLACE;
	f->blend_info.alpha_chan = 0; // XXX set to the actual alpha channel
	f->blend_info.clamp = 0;
	f->blend_info.src_ref_frame = 0;
	f->ec_blend_info = NULL;
	f->save_as_ref = 0;
	f->save_before_ct = 1;
	f->name_len = 0;
	f->name = NULL;
	f->gab.enabled = 1;
	f->gab.weights[0][0] = f->gab.weights[1][0] = f->gab.weights[2][0] = 0.115169525f;
	f->gab.weights[0][1] = f->gab.weights[1][1] = f->gab.weights[2][1] = 0.061248592f;
	f->epf.iters = 2;
	for (i = 0; i < 8; ++i) f->epf.sharp_lut[i] = (float) i / 7.0f;
	f->epf.channel_scale[0] = 40.0f;
	f->epf.channel_scale[1] = 5.0f;
	f->epf.channel_scale[2] = 3.5f;
	f->epf.quant_mul = 0.46f;
	f->epf.pass0_sigma_scale = 0.9f;
	f->epf.pass2_sigma_scale = 6.5f;
	f->epf.border_sad_mul = 2.0f / 3.0f;
	f->epf.sigma_for_modular = 1.0f;
	// TODO spec bug: default values for m_*_lf_unscaled should be reciprocals of the listed values
	f->m_lf_scaled[0] = 1.0f / 4096.0f;
	f->m_lf_scaled[1] = 1.0f / 512.0f;
	f->m_lf_scaled[2] = 1.0f / 256.0f;
	f->global_tree = NULL;
	memset(&f->global_codespec, 0, sizeof(microjxl__code_spec));
	memset(&f->gmodular, 0, sizeof(microjxl__modular));
	f->block_ctx_map = NULL;
	memset(&f->render_f16, 0, sizeof(f->render_f16));
	memset(&f->vardct_f, 0, sizeof(f->vardct_f));
	memset(&f->vardct_ss, 0, sizeof(f->vardct_ss));
	f->inv_colour_factor = 1 / 84.0f;
	f->x_factor_lf = 0;
	f->b_factor_lf = 0;
	f->base_corr_x = 0.0f;
	f->base_corr_b = 1.0f;
	f->lf_stash = NULL;
	f->lf_stash_have = 0;
	/* JPEG reconstruction capture is re-armed per frame: only non-XYB
	 * VarDCT regular frames of a jbrd-carrying container capture (LF
	 * frames / modular frames / XYB frames cannot reconstruct a JPEG). */
	f->jpeg_recon = (st->container->jbrd && !im->xyb_encoded && !f->is_modular &&
		(f->type == MICROJXL__FRAME_REGULAR || f->type == MICROJXL__FRAME_REGULAR_SKIPPROG));
	f->jpeg_recon_alloc = 0;
	f->jpeg_qtable_ok = 0;
	f->jpeg_dc_prec_shift = 0;
	f->jpeg_dcv_w[0] = f->jpeg_dcv_w[1] = f->jpeg_dcv_w[2] = 0;
	f->jpeg_dcv_h[0] = f->jpeg_dcv_h[1] = f->jpeg_dcv_h[2] = 0;
	f->jpeg_wib[0] = f->jpeg_wib[1] = f->jpeg_wib[2] = 0;
	f->jpeg_hib[0] = f->jpeg_hib[1] = f->jpeg_hib[2] = 0;
	f->jpeg_coeffs[0] = f->jpeg_coeffs[1] = f->jpeg_coeffs[2] = NULL;
	f->jpeg_dcv[0] = f->jpeg_dcv[1] = f->jpeg_dcv[2] = NULL;
	f->dct_select_used = f->dct_select_loaded = 0;
	f->order_used = f->order_loaded = 0;
	memset(f->dq_matrix, 0, sizeof(f->dq_matrix));
	memset(f->orders, 0, sizeof(f->orders));
	memset(f->coeff_codespec, 0, sizeof(f->coeff_codespec));

	MICROJXL__TRY(microjxl__zero_pad_to_byte(st));

	if (!microjxl__u(st, 1)) { // !all_default
		int full_frame = 1;
		uint64_t flags;
		f->type = (enum microjxl__frame_type) microjxl__u(st, 2);
		f->is_modular = microjxl__u(st, 1);
		flags = microjxl__u64(st);
		f->has_noise = (int) (flags & 1);
		f->has_patches = (int) (flags >> 1 & 1);
		f->has_splines = (int) (flags >> 4 & 1);
		f->use_lf_frame = (int) (flags >> 5 & 1);
		f->skip_adapt_lf_smooth = (int) (flags >> 7 & 1);
#ifdef MICROJXL_DEBUG
		fprintf(stderr, "[microjxl] flags: noise=%d patches=%d splines=%d use_lf=%d skip_adapt=%d\n",
			f->has_noise, f->has_patches, f->has_splines, f->use_lf_frame, f->skip_adapt_lf_smooth);
#endif
		if (!im->xyb_encoded) f->do_ycbcr = microjxl__u(st, 1);
		if (!f->use_lf_frame) {
			if (f->do_ycbcr) {
			f->jpeg_upsampling = microjxl__u(st, 6); // yes, we are lazy
			/* spec 18181-1 9.3 / L.2: 3 x 2-bit channel modes, each denoting
			 * {horizontal, vertical} sampling factors of {1,1}, {2,2}, {2,1}, {1,2}.
			 * The effective subsampling shift of a channel is the difference between
			 * the maximum sampling factor across channels and its own, so that the
			 * most-sampled channel ends up with shift 0 (full resolution). */
			static const int32_t kHShift[4] = {0, 1, 1, 0};
			static const int32_t kVShift[4] = {0, 1, 0, 1};
			int32_t mode[3], maxh = 0, maxv = 0, cc;
			for (cc = 0; cc < 3; ++cc) {
				mode[cc] = (f->jpeg_upsampling >> (2 * cc)) & 3;
				if (kHShift[mode[cc]] > maxh) maxh = kHShift[mode[cc]];
				if (kVShift[mode[cc]] > maxv) maxv = kVShift[mode[cc]];
			}
			/* the modes are stored in JXL channel order 0=Cb, 1=Y, 2=Cr, which is
			 * exactly microjxl's internal XYB order (0=X/Cb, 1=Y, 2=B/Cr), so the
			 * effective shift is a direct mapping per channel */
			for (cc = 0; cc < 3; ++cc) {
				f->jpeg_hshift[cc] = maxh - kHShift[mode[cc]];
				f->jpeg_vshift[cc] = maxv - kVShift[mode[cc]];
			}
#ifdef MICROJXL_DEBUG
			fprintf(stderr, "[microjxl] jpeg_upsampling=%d hshift=%d/%d/%d vshift=%d/%d/%d\n",
				f->jpeg_upsampling, f->jpeg_hshift[0], f->jpeg_hshift[1], f->jpeg_hshift[2],				f->jpeg_vshift[0], f->jpeg_vshift[1], f->jpeg_vshift[2]);
			#endif
			}
			f->log_upsampling = microjxl__u(st, 2);
			MICROJXL__TRY_MALLOC(int32_t, &f->ec_log_upsampling, (size_t) im->num_extra_channels);
			for (i = 0; i < im->num_extra_channels; ++i) {
				f->ec_log_upsampling[i] = microjxl__u(st, 2);
			}
		} else {
			/* 18181-1 F.2: upsampling / ec_upsampling are not parsed when
			 * kUseLfFrame is set (the consuming frame's LF comes
			 * pre-downsampled from the LF frame; libjxl frame_header.cc
			 * Conditional((flags & kUseDcFrame) == 0)). Defaults (1, i.e.
			 * log 0) apply. */
			f->log_upsampling = 0;
			MICROJXL__TRY_MALLOC(int32_t, &f->ec_log_upsampling, (size_t) im->num_extra_channels);
			for (i = 0; i < im->num_extra_channels; ++i) f->ec_log_upsampling[i] = 0;
		}
#ifdef MICROJXL_DEBUG
		fprintf(stderr, "[microjxl] frame: modular=%d ycbcr=%d xyb=%d cspace=%d bpp=%d\n",
			f->is_modular, f->do_ycbcr, im->xyb_encoded, im->cspace, im->bpp);
		fprintf(stderr, "[microjxl] qms: x=%d b=%d (xyb=%d)\n", f->x_qm_scale, f->b_qm_scale, im->xyb_encoded);
		if (f->do_ycbcr) fprintf(stderr, "[microjxl] jpeg_upsampling=%d\n", f->jpeg_upsampling);
#endif
		if (f->is_modular) {
			f->group_size_shift = 7 + microjxl__u(st, 2);
		} else if (im->xyb_encoded) {
			f->x_qm_scale = microjxl__u(st, 3);
			f->b_qm_scale = microjxl__u(st, 3);
		} else {
			/* libjxl frame_header.cc: for non-XYB VarDCT frames (e.g. YCbCr /
			 * JPEG recompression) x_qm_scale and b_qm_scale are forced to 2,
			 * making both per-channel dequant multipliers exactly 1.0. */
			f->x_qm_scale = 2;
			f->b_qm_scale = 2;
		}
		if (f->type != MICROJXL__FRAME_REFONLY) {
			f->num_passes = microjxl__u32(st, 1, 0, 2, 0, 3, 0, 4, 3);
			if (f->num_passes > 1) {
				// SPEC this part is especially flaky and the spec and libjxl don't agree to each other.
				// we do the most sensible thing that is still compatible to libjxl:
				// - downsample should be decreasing (or stay same)
				// - last_pass should be strictly increasing and last_pass[0] (if any) should be 0
				// see also https://github.com/libjxl/libjxl/issues/1401
				int8_t log_ds[4];
				int32_t ppass = 0, num_ds = microjxl__u32(st, 0, 0, 1, 0, 2, 0, 3, 1);
				MICROJXL__SHOULD(num_ds < f->num_passes, "pass");
				f->num_ds = (int8_t) num_ds;
				for (i = 0; i < f->num_passes - 1; ++i) f->shift[i] = (int8_t) microjxl__u(st, 2);
				f->shift[f->num_passes - 1] = 0;
				for (i = 0; i < num_ds; ++i) {
					log_ds[i] = (int8_t) microjxl__u(st, 2);
					f->ds_log[i] = log_ds[i];
					if (i > 0) MICROJXL__SHOULD(log_ds[i - 1] >= log_ds[i], "pass");
				}
				for (i = 0; i < num_ds; ++i) {
					int32_t pass = microjxl__u32(st, 0, 0, 1, 0, 2, 0, 0, 3);
					f->ds_last_pass[i] = (int8_t) pass;
					MICROJXL__SHOULD(i > 0 ? ppass < pass && pass < f->num_passes : pass == 0, "pass");
					while (ppass < pass) f->log_ds[++ppass] = i > 0 ? log_ds[i - 1] : 3;
				}
				while (ppass < f->num_passes) f->log_ds[++ppass] = i > 0 ? log_ds[num_ds - 1] : 3;
			}
		}
		if (f->type == MICROJXL__FRAME_LF) {
			f->lf_level = microjxl__u(st, 2) + 1;
			/* frame_header.cc U32(Val(1), Val(2), Val(3), Val(4)): level 0
			 * (the raw 2-bit value would be 4) is invalid. */
			MICROJXL__SHOULD(f->lf_level <= 4, "flvl");
		} else if (microjxl__u(st, 1)) { // have_crop
			if (f->type != MICROJXL__FRAME_REFONLY) { // SPEC missing UnpackSigned
				f->x0 = microjxl__unpack_signed(microjxl__u32(st, 0, 8, 256, 11, 2304, 14, 18688, 30));
				f->y0 = microjxl__unpack_signed(microjxl__u32(st, 0, 8, 256, 11, 2304, 14, 18688, 30));
			}
			f->width = microjxl__u32(st, 0, 8, 256, 11, 2304, 14, 18688, 30);
			f->height = microjxl__u32(st, 0, 8, 256, 11, 2304, 14, 18688, 30);
			MICROJXL__SHOULD(f->width <= st->limits->width && f->height <= st->limits->height, "slim");
			MICROJXL__SHOULD((int64_t) f->width * f->height <= st->limits->pixels, "slim");
			full_frame = f->x0 <= 0 && f->y0 <= 0 &&
				f->width + f->x0 >= im->width && f->height + f->y0 >= im->height;
		}
		if (f->type == MICROJXL__FRAME_REGULAR || f->type == MICROJXL__FRAME_REGULAR_SKIPPROG) {
			MICROJXL__TRY_MALLOC(microjxl__blend_info, &f->ec_blend_info, (size_t) im->num_extra_channels);
			for (i = -1; i < im->num_extra_channels; ++i) {
				microjxl__blend_info *blend = i < 0 ? &f->blend_info : &f->ec_blend_info[i];
				/* libjxl's defaults: alpha_channel = 0, clamp = 0 (fields keep
				 * their defaults when the mode does not code them) */
				blend->alpha_chan = 0;
				blend->clamp = 0;
				blend->mode = (int8_t) microjxl__u32(st, 0, 0, 1, 0, 2, 0, 3, 2);
				if (im->num_extra_channels > 0 && (blend->mode == MICROJXL__BLEND_BLEND || blend->mode == MICROJXL__BLEND_MUL_ADD)) {
					blend->alpha_chan = (int8_t) microjxl__u32(st, 0, 0, 1, 0, 2, 0, 3, 3);
					blend->clamp = (int8_t) microjxl__u(st, 1);
				} else if (blend->mode == MICROJXL__BLEND_MUL) {
					/* libjxl BlendingInfo::VisitFields reads clamp for kMul even
					 * when there are no extra channels: the condition is
					 * (nextra > 0 && (kBlend||kAlphaWeightedAdd)) || kMul. */
					blend->clamp = (int8_t) microjxl__u(st, 1);
				}
				if (!full_frame || blend->mode != MICROJXL__BLEND_REPLACE) {
					blend->src_ref_frame = (int8_t) microjxl__u(st, 2);
				}
			}
			if (getenv("MICROJXL_TRACE_BLEND")) fprintf(stderr, "[blend] frame type=%d mode=%d clamp=%d srcref=%d save_as_ref=%d\n", (int) f->type, (int) f->blend_info.mode, (int) f->blend_info.clamp, (int) f->blend_info.src_ref_frame, (int) f->save_as_ref);
			if (im->anim_tps_denom) { // have_animation stored implicitly
				f->duration = microjxl__64u32(st, 0, 0, 1, 0, 0, 8, 0, 32);
				if (im->anim_have_timecodes) {
					f->timecode = microjxl__64u(st, 32);
				}
			}
			f->is_last = microjxl__u(st, 1);
		} else {
			f->is_last = 0;
		}
		if (f->type != MICROJXL__FRAME_LF && !f->is_last) f->save_as_ref = microjxl__u(st, 2);
		// SPEC this condition is essentially swapped with the default value in the spec
		if (f->type == MICROJXL__FRAME_REFONLY || (
			full_frame &&
			(f->type == MICROJXL__FRAME_REGULAR || f->type == MICROJXL__FRAME_REGULAR_SKIPPROG) &&
			f->blend_info.mode == MICROJXL__BLEND_REPLACE &&
			(f->duration == 0 || f->save_as_ref != 0) &&
			!f->is_last
		)) {
			f->save_before_ct = microjxl__u(st, 1);
		} else {
			f->save_before_ct = (f->type == MICROJXL__FRAME_LF);
		}
		MICROJXL__TRY(microjxl__name(st, &f->name_len, &f->name));
		{ // RestorationFilter
			int restoration_all_default = microjxl__u(st, 1);
			/* libjxl's LoopFilter::AllDefault skips the whole LoopFilter body:
			 * when set, gab/epf sub-flags are NOT read (previously we defaulted
			 * the values but still consumed their flags, misaligning every
			 * subsequent parse). */
			if (restoration_all_default) {
				f->gab.enabled = 1;
				f->epf.iters = 2;
			} else {
			f->gab.enabled = microjxl__u(st, 1);
			if (f->gab.enabled) {
				if (microjxl__u(st, 1)) { // gab_custom
					for (i = 0; i < 3; ++i) {
						for (j = 0; j < 2; ++j) f->gab.weights[i][j] = microjxl__f16(st);
					}
				}
			}
			f->epf.iters = restoration_all_default ? 2 : microjxl__u(st, 2);
#ifdef MICROJXL_DEBUG
			fprintf(stderr, "[jepf] iters=%d gab=%d\n", f->epf.iters, f->gab.enabled);
#endif
			if (f->epf.iters) {
				if (!f->is_modular && microjxl__u(st, 1)) { // epf_sharp_custom
					for (i = 0; i < 8; ++i) f->epf.sharp_lut[i] = microjxl__f16(st);
				}
				if (microjxl__u(st, 1)) { // epf_weight_custom
					for (i = 0; i < 3; ++i) f->epf.channel_scale[i] = microjxl__f16(st);
					MICROJXL__TRY(microjxl__skip(st, 32)); // ignored
				}
				if (microjxl__u(st, 1)) { // epf_sigma_custom
					if (!f->is_modular) f->epf.quant_mul = microjxl__f16(st);
					f->epf.pass0_sigma_scale = microjxl__f16(st);
					f->epf.pass2_sigma_scale = microjxl__f16(st);
					f->epf.border_sad_mul = microjxl__f16(st);
				}
				if (f->epf.iters && f->is_modular) {
					f->epf.sigma_for_modular = microjxl__f16(st);
#ifdef MICROJXL_DEBUG
					fprintf(stderr, "[microjxl] sigma_for_modular=%g\n", (double) f->epf.sigma_for_modular);
#endif
				}
			}
			if (!restoration_all_default) MICROJXL__TRY(microjxl__extensions(st));
			} // end else (!restoration_all_default)
		}
		MICROJXL__TRY(microjxl__extensions(st));
	}
	MICROJXL__RAISE_DELAYED();
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_TOC")) fprintf(stderr, "[microjxl-fh] type=%d last=%d modular=%d x0=%d y0=%d w=%d h=%d dur=%lld save_as_ref=%d save_before_ct=%d name_len=%d gab=%d epf=%d num_passes=%d bitpos=%lld\n",
		f->type, f->is_last, f->is_modular, f->x0, f->y0, f->width, f->height,
		(long long) f->duration, f->save_as_ref, f->save_before_ct, f->name_len,
		f->gab.enabled, f->epf.iters, f->num_passes, (long long) microjxl__bits_read(st));
#endif

	if (im->xyb_encoded && im->want_icc) f->save_before_ct = 1; // ignores the decoded bit
	if (f->use_lf_frame) {
		/* libjxl passes_state.cc: a kUseDcFrame frame with header dc_level
		 * D consumes the DC frame whose header dc_level is D+1 (regular
		 * frames have dc_level 0 and consume the level-1 LF frame; a level-1
		 * LF frame consumes the level-2 one, and so on). D == 4 is rejected
		 * (there is no level-5 frame to consume). microjxl stores the frame
		 * with lf_level L at lf_frames[L - 1], so the consumer reads
		 * lf_frames[f->lf_level]. */
		MICROJXL__SHOULD(f->lf_level <= 3, "flvl");
	}
	if (f->type == MICROJXL__FRAME_LF) {
		/* 18181-1 9.3 / frame_header.h ToFrameDimensions: an LF frame's
		 * dimensions are the image's size downsampled by 8^lf_level (the
		 * frame carries the LF, i.e. 8x-downsampled samples, per level of
		 * the pyramid). Group geometry below operates on this size. */
		int32_t shift = 3 * f->lf_level;
		f->upsampled_width = f->width;
		f->upsampled_height = f->height;
		f->width = microjxl__ceil_div32(f->width, 1 << shift);
		f->height = microjxl__ceil_div32(f->height, 1 << shift);
		MICROJXL__SHOULD(f->width >= 1 && f->height >= 1, "slim");
	} else if (f->log_upsampling > 0) {
		/* The frame dimensions above are the final (upsampled) size; the
		 * frame data itself is stored at ceil(size / 2^log_upsampling) and
		 * upsampled back after decoding. All group geometry and plane
		 * allocation below operates on the stored size. */
		f->upsampled_width = f->width;
		f->upsampled_height = f->height;
		f->width = microjxl__ceil_div32(f->width, 1 << f->log_upsampling);
		f->height = microjxl__ceil_div32(f->height, 1 << f->log_upsampling);
	} else {
		f->upsampled_width = f->width;
		f->upsampled_height = f->height;
	}
	f->grows = microjxl__ceil_div32(f->height, 1 << f->group_size_shift);
	f->gcolumns = microjxl__ceil_div32(f->width, 1 << f->group_size_shift);
	f->num_groups = (int64_t) f->grows * f->gcolumns;
	f->ggrows = microjxl__ceil_div32(f->height, 8 << f->group_size_shift);
	f->ggcolumns = microjxl__ceil_div32(f->width, 8 << f->group_size_shift);
	f->num_lf_groups = (int64_t) f->ggrows * f->ggcolumns;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(f->ec_log_upsampling);
	microjxl__mem_free(f->ec_blend_info);
	microjxl__mem_free(f->name);
	f->ec_log_upsampling = NULL;
	f->ec_blend_info = NULL;
	f->name = NULL;
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// frame header

typedef struct {
	int64_t idx; // either LF group index (pass < 0) or group index (pass >= 0)
	int64_t codeoff;
	int32_t size;
	int32_t pass; // pass number, or negative if this is an LF group section
} microjxl__section;

typedef struct {
	// if nonzero, there is only single section of this size and other fields are ignored
	int32_t single_size;

	// LfGlobal and HfGlobal are common dependencies of other sections, and handled separately
	int64_t lf_global_codeoff, hf_global_codeoff;
	int32_t lf_global_size, hf_global_size;

	// other sections are ordered by codeoff, unless the earlier section needs the later section
	// for decoding, in which case the order gets swapped
	int64_t nsections, nsections_read;
	microjxl__section *sections;
	int64_t end_codeoff;
} microjxl__toc;

MICROJXL__STATIC_RETURNS_ERR microjxl__permutation(
	microjxl__st *st, microjxl__code_st *code, int32_t size, int32_t skip, int32_t **out
);
MICROJXL_INLINE void microjxl__apply_permutation(void *targetbuf, void *temp, size_t elemsize, const int32_t *lehmer);
MICROJXL__STATIC_RETURNS_ERR microjxl__read_toc(microjxl__st *st, microjxl__toc *toc);
MICROJXL_STATIC void microjxl__mem_free_toc(microjxl__toc *toc);

#ifdef MICROJXL_IMPLEMENTATION

// also used in microjxl__hf_global; out is terminated by a sentinel (-1) or NULL if empty
// TODO permutation may have to handle more than 2^31 entries
MICROJXL__STATIC_RETURNS_ERR microjxl__permutation(
	microjxl__st *st, microjxl__code_st *code, int32_t size, int32_t skip, int32_t **out
) {
	int32_t *arr = NULL;
	int32_t i, prev, end;

	MICROJXL__ASSERT(code->spec->num_dist == 8 + !!code->spec->lz77_enabled);

	// SPEC this is the number of integers to read, not the last offset to read (can differ when skip > 0)
	end = microjxl__code(st, microjxl__min32(7, microjxl__ceil_lg32((uint32_t) size + 1)), 0, code);
	MICROJXL__SHOULD(end <= size - skip, "perm"); // SPEC missing
	if (end == 0) {
		*out = NULL;
		return 0;
	}

	MICROJXL__TRY_MALLOC(int32_t, &arr, (size_t) (end + 1));
	prev = 0;
	for (i = 0; i < end; ++i) {
		prev = arr[i] = microjxl__code(st, microjxl__min32(7, microjxl__ceil_lg32((uint32_t) prev + 1)), 0, code);
		MICROJXL__SHOULD(prev < size - (skip + i), "perm"); // SPEC missing
	}
	arr[end] = -1; // sentinel
	*out = arr;
	return 0;

MICROJXL__ON_ERROR:
	free(arr);
	return st->err;
}

// target is pre-shifted by skip
MICROJXL_INLINE void microjxl__apply_permutation(
	void *targetbuf, void *temp, size_t elemsize, const int32_t *lehmer
) {
	char *target = (char*) targetbuf;
	if (!lehmer) return;
	while (*lehmer >= 0) {
		size_t x = (size_t) *lehmer++;
		memcpy(temp, target + elemsize * x, elemsize);
		memmove(target + elemsize, target, elemsize * x);
		memcpy(target, temp, elemsize);
		target += elemsize;
	}
}

MICROJXL_STATIC int microjxl__compare_section(const void *a, const void *b) {
	const microjxl__section *aa = (const microjxl__section*) a, *bb = (const microjxl__section*) b;
	return aa->codeoff < bb->codeoff ? -1 : aa->codeoff > bb->codeoff ? 1 : 0;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__read_toc(microjxl__st *st, microjxl__toc *toc) {
	microjxl__frame_st *f = st->frame;

	int64_t nsections = f->num_passes == 1 && f->num_groups == 1 ? 1 :
		1 /*lf_global*/ + f->num_lf_groups /*lf_group*/ +
		1 /*hf_global + hf_pass*/ + f->num_passes * f->num_groups /*group_pass*/;
	int64_t nsections2;
	microjxl__section *sections = NULL, *sections2 = NULL, temp;

	memset(toc, 0, sizeof *toc); // reset all fields, incl. single_size/nsections_read

	// interleaved linked lists for each LF group; for each LF group `gg` there are three cases:
	// - no relocated section if `relocs[gg].next == 0` (initial state).
	// - a single relocated section `relocs[gg].section` if `relocs[gg].next < 0`.
	// - 2+ relocated sections `relocs[i].section`, where `k` starts at `gg` and
	//   continues through `next` until it's negative.
	struct reloc { int64_t next; microjxl__section section; } *relocs = NULL;
	int64_t nrelocs, relocs_cap;

	int32_t *lehmer = NULL;
	microjxl__code_spec codespec = MICROJXL__INIT;
	microjxl__code_st code = MICROJXL__INIT;
	int64_t i, nremoved;
	int32_t pass;

	// TODO remove int32_t restrictions
	MICROJXL__SHOULD((uint64_t) nsections <= SIZE_MAX && nsections <= INT32_MAX, "flen");

	if (microjxl__u(st, 1)) { // permuted
		MICROJXL__TRY(microjxl__read_code_spec(st, 8, &codespec));
		microjxl__init_code(&code, &codespec);
		MICROJXL__TRY(microjxl__permutation(st, &code, (int32_t) nsections, 0, &lehmer));
		MICROJXL__TRY(microjxl__finish_and_free_code(st, &code));
		microjxl__mem_free_code_spec(&codespec);
	}
	MICROJXL__TRY(microjxl__zero_pad_to_byte(st));

	// single section case: no allocation required
	if (nsections == 1) {
		toc->single_size = microjxl__u32(st, 0, 10, 1024, 14, 17408, 22, 4211712, 30);
		MICROJXL__TRY(microjxl__zero_pad_to_byte(st));
		toc->lf_global_codeoff = toc->hf_global_codeoff = 0;
		toc->lf_global_size = toc->hf_global_size = 0;
		toc->nsections = toc->nsections_read = 0;
		toc->sections = NULL;
		MICROJXL__SHOULD(microjxl__add64(microjxl__codestream_offset(st), toc->single_size, &toc->end_codeoff), "flen");
		microjxl__mem_free(lehmer);
		return 0;
	}

	MICROJXL__TRY_MALLOC(microjxl__section, &sections, (size_t) nsections);
	for (i = 0; i < nsections; ++i) {
		sections[i].size = microjxl__u32(st, 0, 10, 1024, 14, 17408, 22, 4211712, 30);
	}
	MICROJXL__TRY(microjxl__zero_pad_to_byte(st));

	sections[0].codeoff = microjxl__codestream_offset(st); // all TOC offsets are relative to this point
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_TOC")) fprintf(stderr, "[microjxl-toc] sections start bitpos=%lld\n", (long long) microjxl__bits_read(st));
#endif
	for (i = 1; i < nsections; ++i) {
		MICROJXL__SHOULD(microjxl__add64(sections[i-1].codeoff, sections[i-1].size, &sections[i].codeoff), "flen");
	}
	MICROJXL__SHOULD(microjxl__add64(sections[i-1].codeoff, sections[i-1].size, &toc->end_codeoff), "flen");

	if (lehmer) {
		microjxl__apply_permutation(sections, &temp, sizeof(microjxl__section), lehmer);
		microjxl__mem_free(lehmer);
		lehmer = NULL;
	}

	toc->lf_global_codeoff = sections[0].codeoff;
	toc->lf_global_size = sections[0].size;
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_TOC")) fprintf(stderr, "[microjxl-toc] single=%d lfg=%lld+%d hfg=%lld+%d nsections=%lld\n", toc->single_size, (long long) toc->lf_global_codeoff, toc->lf_global_size, (long long) toc->hf_global_codeoff, toc->hf_global_size, (long long) nsections);
#endif
	sections[0].codeoff = -1;
	for (i = 0; i < f->num_lf_groups; ++i) {
		sections[i + 1].pass = -1;
		sections[i + 1].idx = i;
	}
	toc->hf_global_codeoff = sections[f->num_lf_groups + 1].codeoff;
	toc->hf_global_size = sections[f->num_lf_groups + 1].size;
	sections[f->num_lf_groups + 1].codeoff = -1;
	for (pass = 0; pass < f->num_passes; ++pass) {
		int64_t sectionid = 1 + f->num_lf_groups + 1 + pass * f->num_groups;
		for (i = 0; i < f->num_groups; ++i) {
			sections[sectionid + i].pass = pass;
			sections[sectionid + i].idx = i;
		}
	}

	// any group section depending on the later LF group section is temporarily moved to relocs
	{
		int32_t ggrow, ggcolumn;

		MICROJXL__TRY_CALLOC(struct reloc, &relocs, (size_t) f->num_lf_groups);
		nrelocs = relocs_cap = f->num_lf_groups;

		for (ggrow = 0; ggrow < f->ggrows; ++ggrow) for (ggcolumn = 0; ggcolumn < f->ggcolumns; ++ggcolumn) {
			int64_t ggidx = (int64_t) ggrow * f->ggcolumns + ggcolumn, ggsection = 1 + ggidx;
			int64_t ggcodeoff = sections[ggsection].codeoff;
			int64_t gsection_base =
				1 + f->num_lf_groups + 1 + (int64_t) (ggrow * 8) * f->gcolumns + (ggcolumn * 8);
			int32_t grows_in_gg = microjxl__min32((ggrow + 1) * 8, f->grows) - ggrow * 8;
			int32_t gcolumns_in_gg = microjxl__min32((ggcolumn + 1) * 8, f->gcolumns) - ggcolumn * 8;
			int32_t grow_in_gg, gcolumn_in_gg;

			for (pass = 0; pass < f->num_passes; ++pass) {
				for (grow_in_gg = 0; grow_in_gg < grows_in_gg; ++grow_in_gg) {
					for (gcolumn_in_gg = 0; gcolumn_in_gg < gcolumns_in_gg; ++gcolumn_in_gg) {
						int64_t gsection = gsection_base + pass * f->num_groups + 
							(grow_in_gg * f->gcolumns + gcolumn_in_gg);
						if (sections[gsection].codeoff > ggcodeoff) continue;
						if (relocs[ggidx].next) {
							MICROJXL__TRY_REALLOC64(struct reloc, &relocs, nrelocs + 1, &relocs_cap);
							relocs[nrelocs] = relocs[ggidx];
							relocs[ggidx].next = nrelocs++;
						} else {
							relocs[ggidx].next = -1;
						}
						relocs[ggidx].section = sections[gsection];
						sections[gsection].codeoff = -1;
					}
				}
			}
		}
	}

	// remove any section with a codeoff -1 and sort the remainder
	for (i = nremoved = 0; i < nsections; ++i) {
		if (sections[i].codeoff < 0) {
			++nremoved;
		} else {
			sections[i - nremoved] = sections[i];
		}
	}
	qsort(sections, (size_t) (nsections - nremoved), sizeof(microjxl__section), microjxl__compare_section);

	// copy sections to sections2, but insert any relocated sections after corresponding LF group section
	MICROJXL__TRY_MALLOC(microjxl__section, &sections2, (size_t) nsections);
	nsections2 = 0;
	for (i = 0; i < nsections - nremoved; ++i) {
		int64_t j, first_reloc_off;
		sections2[nsections2++] = sections[i];
		if (sections[i].pass >= 0) continue;
		j = sections[i].idx;
		if (!relocs[j].next) continue;
		first_reloc_off = nsections2;
		while (j >= 0) {
			sections2[nsections2++] = relocs[j].section;
			j = relocs[j].next;
		}
		qsort(sections2 + first_reloc_off, (size_t) (nsections2 - first_reloc_off),
			sizeof(microjxl__section), microjxl__compare_section);
	}

	toc->sections = sections2;
	toc->nsections = nsections2;
	toc->nsections_read = 0;
	MICROJXL__ASSERT(nsections2 == nsections - 2); // excludes LfGlobal and HfGlobal
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_TOC")) {
		for (i = 0; i < nsections2; ++i) {
			fprintf(stderr, "[microjxl-toc] section %d: off=%lld size=%d pass=%d idx=%lld\n", (int) i, (long long) sections2[i].codeoff, sections2[i].size, sections2[i].pass, (long long) sections2[i].idx);
		}
	}
#endif

	microjxl__mem_free(sections);
	microjxl__mem_free(relocs);
	microjxl__mem_free(lehmer);
	microjxl__mem_free_code(&code);
	microjxl__mem_free_code_spec(&codespec);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(sections);
	microjxl__mem_free(sections2);
	microjxl__mem_free(relocs);
	microjxl__mem_free(lehmer);
	microjxl__mem_free_code(&code);
	microjxl__mem_free_code_spec(&codespec);
	return st->err;
}

MICROJXL_STATIC void microjxl__mem_free_toc(microjxl__toc *toc) {
	microjxl__mem_free(toc->sections);
	toc->sections = NULL;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// DCT

// both use `in` as a scratch space as well, so `in` will be altered after return
MICROJXL_STATIC void microjxl__forward_dct_unscaled(
	float *MICROJXL_RESTRICT out, float *MICROJXL_RESTRICT in, int32_t t, int32_t rep
);
MICROJXL_STATIC void microjxl__inverse_dct(
	float *MICROJXL_RESTRICT out, float *MICROJXL_RESTRICT in, int32_t t, int32_t rep
);

MICROJXL_STATIC void microjxl__forward_dct2d_scaled_for_llf(
	float *MICROJXL_RESTRICT buf, float *MICROJXL_RESTRICT scratch, int32_t log_rows, int32_t log_columns
);
MICROJXL_STATIC void microjxl__inverse_dct2d(
	float *MICROJXL_RESTRICT buf, float *MICROJXL_RESTRICT scratch, int32_t log_rows, int32_t log_columns
);

MICROJXL_STATIC void microjxl__inverse_dct11(float *buf);
MICROJXL_STATIC void microjxl__inverse_dct22(float *buf);
MICROJXL_STATIC void microjxl__inverse_hornuss(float *buf);
MICROJXL_STATIC void microjxl__inverse_dct32(float *buf);
MICROJXL_STATIC void microjxl__inverse_dct23(float *buf);
MICROJXL_STATIC void microjxl__inverse_afv22(float *MICROJXL_RESTRICT out, float *MICROJXL_RESTRICT in);
MICROJXL_STATIC void microjxl__inverse_afv(float *buf, int flipx, int flipy);

#ifdef MICROJXL_IMPLEMENTATION

// this is more or less a direct translation of mcos2/3 algorithms described in:
// Perera, S. M., & Liu, J. (2018). Lowest Complexity Self-Recursive Radix-2 DCT II/III Algorithms.
// SIAM Journal on Matrix Analysis and Applications, 39(2), 664--682.

// [(1<<n) + k] = 1/(2 cos((k+0.5)/2^(n+1) pi)) for n >= 1 and 0 <= k < 2^n
MICROJXL_STATIC const float MICROJXL__HALF_SECANTS[256] = {
	0, 0, // unused
	0.54119610f, 1.30656296f, // n=1 for DCT-4
	0.50979558f, 0.60134489f, 0.89997622f, 2.56291545f, // n=2 for DCT-8
	// n=3 for DCT-16
	0.50241929f, 0.52249861f, 0.56694403f, 0.64682178f, 0.78815462f, 1.06067769f, 1.72244710f, 5.10114862f,
	// n=4 for DCT-32
	0.50060300f, 0.50547096f, 0.51544731f, 0.53104259f, 0.55310390f, 0.58293497f, 0.62250412f, 0.67480834f,
	0.74453627f, 0.83934965f, 0.97256824f, 1.16943993f, 1.48416462f, 2.05778101f, 3.40760842f, 10.1900081f,
	// n=5 for DCT-64
	0.50015064f, 0.50135845f, 0.50378873f, 0.50747117f, 0.51245148f, 0.51879271f, 0.52657732f, 0.53590982f,
	0.54692044f, 0.55976981f, 0.57465518f, 0.59181854f, 0.61155735f, 0.63423894f, 0.66031981f, 0.69037213f,
	0.72512052f, 0.76549416f, 0.81270209f, 0.86834472f, 0.93458360f, 1.01440826f, 1.11207162f, 1.23383274f,
	1.38929396f, 1.59397228f, 1.87467598f, 2.28205007f, 2.92462843f, 4.08461108f, 6.79675071f, 20.3738782f,
	// n=6 for DCT-128
	0.50003765f, 0.50033904f, 0.50094272f, 0.50185052f, 0.50306519f, 0.50459044f, 0.50643095f, 0.50859242f,
	0.51108159f, 0.51390633f, 0.51707566f, 0.52059987f, 0.52449054f, 0.52876071f, 0.53342493f, 0.53849944f,
	0.54400225f, 0.54995337f, 0.55637499f, 0.56329167f, 0.57073059f, 0.57872189f, 0.58729894f, 0.59649876f,
	0.60636246f, 0.61693573f, 0.62826943f, 0.64042034f, 0.65345190f, 0.66743520f, 0.68245013f, 0.69858665f,
	0.71594645f, 0.73464482f, 0.75481294f, 0.77660066f, 0.80017990f, 0.82574877f, 0.85353675f, 0.88381100f,
	0.91688445f, 0.95312587f, 0.99297296f, 1.03694904f, 1.08568506f, 1.13994868f, 1.20068326f, 1.26906117f,
	1.34655763f, 1.43505509f, 1.53699410f, 1.65559652f, 1.79520522f, 1.96181785f, 2.16395782f, 2.41416000f,
	2.73164503f, 3.14746219f, 3.71524274f, 4.53629094f, 5.82768838f, 8.15384860f, 13.5842903f, 40.7446881f,
	// n=7 for DCT-256
	0.50000941f, 0.50008472f, 0.50023540f, 0.50046156f, 0.50076337f, 0.50114106f, 0.50159492f, 0.50212529f,
	0.50273257f, 0.50341722f, 0.50417977f, 0.50502081f, 0.50594098f, 0.50694099f, 0.50802161f, 0.50918370f,
	0.51042817f, 0.51175599f, 0.51316821f, 0.51466598f, 0.51625048f, 0.51792302f, 0.51968494f, 0.52153769f,
	0.52348283f, 0.52552196f, 0.52765682f, 0.52988922f, 0.53222108f, 0.53465442f, 0.53719139f, 0.53983424f,
	0.54258533f, 0.54544717f, 0.54842239f, 0.55151375f, 0.55472418f, 0.55805673f, 0.56151465f, 0.56510131f,
	0.56882030f, 0.57267538f, 0.57667051f, 0.58080985f, 0.58509780f, 0.58953898f, 0.59413825f, 0.59890075f,
	0.60383188f, 0.60893736f, 0.61422320f, 0.61969575f, 0.62536172f, 0.63122819f, 0.63730265f, 0.64359303f,
	0.65010770f, 0.65685553f, 0.66384594f, 0.67108889f, 0.67859495f, 0.68637535f, 0.69444203f, 0.70280766f,
	0.71148577f, 0.72049072f, 0.72983786f, 0.73954355f, 0.74962527f, 0.76010172f, 0.77099290f, 0.78232026f,
	0.79410679f, 0.80637720f, 0.81915807f, 0.83247799f, 0.84636782f, 0.86086085f, 0.87599311f, 0.89180358f,
	0.90833456f, 0.92563200f, 0.94374590f, 0.96273078f, 0.98264619f, 1.00355728f, 1.02553551f, 1.04865941f,
	1.07301549f, 1.09869926f, 1.12581641f, 1.15448427f, 1.18483336f, 1.21700940f, 1.25117548f, 1.28751481f,
	1.32623388f, 1.36756626f, 1.41177723f, 1.45916930f, 1.51008903f, 1.56493528f, 1.62416951f, 1.68832855f,
	1.75804061f, 1.83404561f, 1.91722116f, 2.00861611f, 2.10949453f, 2.22139378f, 2.34620266f, 2.48626791f,
	2.64454188f, 2.82479140f, 3.03189945f, 3.27231159f, 3.55471533f, 3.89110779f, 4.29853753f, 4.80207601f,
	5.44016622f, 6.27490841f, 7.41356676f, 9.05875145f, 11.6446273f, 16.3000231f, 27.1639777f, 81.4878422f,
};

// TODO spec bug: ScaleF doesn't match with the current libjxl! it turns out that this is actually
// a set of factors for the Arai, Agui, Nakajima DCT & IDCT algorithm, which was only used in
// older versions of libjxl (both the current libjxl and j40 currently use Perera-Liu) and
// not even a resampling algorithm to begin with.
//
// [(1<<N) + k] = 1 / (cos(k/2^(4+N) pi) * cos(k/2^(3+N) pi) * cos(k/2^(2+N) pi) * 2^N)
//                for N >= 1 and 0 <= k < 2^N
MICROJXL_STATIC const float MICROJXL__LF2LLF_SCALES[64] = {
	0, // unused
	1.00000000f, // N=1, n=8
	0.50000000f, 0.55446868f, // N=2, n=16
	0.25000000f, 0.25644002f, 0.27723434f, 0.31763984f, // N=4, n=32
	// N=8, n=64
	0.12500000f, 0.12579419f, 0.12822001f, 0.13241272f, 0.13861717f, 0.14722207f, 0.15881992f, 0.17431123f,
	// N=16, n=128
	0.06250000f, 0.06259894f, 0.06289709f, 0.06339849f, 0.06411001f, 0.06504154f, 0.06620636f, 0.06762155f,
	0.06930858f, 0.07129412f, 0.07361103f, 0.07629973f, 0.07940996f, 0.08300316f, 0.08715562f, 0.09196277f,
	// N=32, n=256
	0.03125000f, 0.03126236f, 0.03129947f, 0.03136146f, 0.03144855f, 0.03156101f, 0.03169925f, 0.03186372f,
	0.03205500f, 0.03227376f, 0.03252077f, 0.03279691f, 0.03310318f, 0.03344071f, 0.03381077f, 0.03421478f,
	0.03465429f, 0.03513107f, 0.03564706f, 0.03620441f, 0.03680552f, 0.03745302f, 0.03814986f, 0.03889931f,
	0.03970498f, 0.04057091f, 0.04150158f, 0.04250201f, 0.04357781f, 0.04473525f, 0.04598138f, 0.04732417f,
};

#define MICROJXL__SQRT2 1.4142135623730951f

#define MICROJXL__DCT_ARGS float *MICROJXL_RESTRICT out, float *MICROJXL_RESTRICT in, int32_t t
#define MICROJXL__REPEAT1() for (r1 = 0; r1 < rep1 * rep2; r1 += rep2)
#define MICROJXL__REPEAT2() for (r2 = 0; r2 < rep2; ++r2)
#define MICROJXL__IN(i) in[(i) * stride + r1 + r2]
#define MICROJXL__OUT(i) out[(i) * stride + r1 + r2]

MICROJXL_ALWAYS_INLINE void microjxl__forward_dct_core(
	MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2,
	void (*half_forward_dct)(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2)
) {
	int32_t r1, r2, i, N = 1 << t, stride = rep1 * rep2;

	// out[0..N) = W^c_N H_N in[0..N)
	MICROJXL__REPEAT1() {
		for (i = 0; i < N / 2; ++i) {
			float mult = MICROJXL__HALF_SECANTS[N / 2 + i];
			MICROJXL__REPEAT2() {
				float x = MICROJXL__IN(i), y = MICROJXL__IN(N - i - 1);
				MICROJXL__OUT(i) = x + y;
				MICROJXL__OUT(N / 2 + i) = (x - y) * mult;
			}
		}
	}

	// in[0..N/2) = mcos2(out[0..N/2), N/2)
	// in[N/2..N) = mcos2(out[N/2..N), N/2)
	half_forward_dct(in, out, t - 1, rep1, rep2);
	half_forward_dct(in + N / 2 * stride, out + N / 2 * stride, t - 1, rep1, rep2);

	// out[0,2..N) = in[0..N/2)
	MICROJXL__REPEAT1() for (i = 0; i < N / 2; ++i) MICROJXL__REPEAT2() {
		MICROJXL__OUT(i * 2) = MICROJXL__IN(i);
	}

	// out[1,3..N) = B_(N/2) in[N/2..N)
	MICROJXL__REPEAT1() {
		MICROJXL__REPEAT2() MICROJXL__OUT(1) = MICROJXL__SQRT2 * MICROJXL__IN(N / 2) + MICROJXL__IN(N / 2 + 1);
		for (i = 1; i < N / 2 - 1; ++i) {
			MICROJXL__REPEAT2() MICROJXL__OUT(i * 2 + 1) = MICROJXL__IN(N / 2 + i) + MICROJXL__IN(N / 2 + i + 1);
		}
		MICROJXL__REPEAT2() MICROJXL__OUT(N - 1) = MICROJXL__IN(N - 1);
	}
}

MICROJXL_ALWAYS_INLINE void microjxl__inverse_dct_core(
	MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2,
	void (*half_inverse_dct)(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2)
) {
	int32_t r1, r2, i, N = 1 << t, stride = rep1 * rep2;

	// out[0..N/2) = in[0,2..N)
	MICROJXL__REPEAT1() {
		for (i = 0; i < N / 2; ++i) {
			MICROJXL__REPEAT2() MICROJXL__OUT(i) = MICROJXL__IN(i * 2);
		}
	}

	// out[N/2..N) = (B_(N/2))^T in[1,3..N)
	MICROJXL__REPEAT1() {
		MICROJXL__REPEAT2() MICROJXL__OUT(N / 2) = MICROJXL__SQRT2 * MICROJXL__IN(1);
		for (i = 1; i < N / 2; ++i) {
			MICROJXL__REPEAT2() MICROJXL__OUT(N / 2 + i) = MICROJXL__IN(i * 2 - 1) + MICROJXL__IN(i * 2 + 1);
		}
	}

	// in[0..N/2) = mcos3(out[0..N/2), N/2)
	// in[N/2..N) = mcos3(out[N/2..N), N/2)
	half_inverse_dct(in, out, t - 1, rep1, rep2);
	half_inverse_dct(in + N / 2 * stride, out + N / 2 * stride, t - 1, rep1, rep2);

	// out[0..N) = (H_N)^T W^c_N in[0..N)
	MICROJXL__REPEAT1() {
		for (i = 0; i < N / 2; ++i) {
			float mult = MICROJXL__HALF_SECANTS[N / 2 + i];
			MICROJXL__REPEAT2() {
				float x = MICROJXL__IN(i), y = MICROJXL__IN(N / 2 + i);
				// this might look wasteful, but modern compilers can optimize them into FMA
				// which can be actually faster than a single multiplication (TODO verify this)
				MICROJXL__OUT(i) = x + y * mult;
				MICROJXL__OUT(N - i - 1) = x - y * mult;
			}
		}
	}
}

MICROJXL_ALWAYS_INLINE void microjxl__dct2(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2) {
	int32_t r1, r2, stride = rep1 * rep2;
	MICROJXL__ASSERT(t == 1); (void) t;
	MICROJXL__REPEAT1() MICROJXL__REPEAT2() {
		float x = MICROJXL__IN(0), y = MICROJXL__IN(1);
		MICROJXL__OUT(0) = x + y;
		MICROJXL__OUT(1) = x - y;
	}
}

MICROJXL_ALWAYS_INLINE void microjxl__forward_dct4(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2) {
	MICROJXL__ASSERT(t == 2); (void) t;
	microjxl__forward_dct_core(out, in, 2, rep1, rep2, microjxl__dct2);
}

MICROJXL_STATIC void microjxl__forward_dct_recur(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2) {
	if (t < 4) {
		MICROJXL__ASSERT(t == 3);
		microjxl__forward_dct_core(out, in, 3, rep1, rep2, microjxl__forward_dct4);
	} else {
		microjxl__forward_dct_core(out, in, t, rep1, rep2, microjxl__forward_dct_recur);
	}
}

MICROJXL_STATIC void microjxl__forward_dct_recur_x8(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2) {
	MICROJXL__ASSERT(rep2 == 8); (void) rep2;
	if (t < 4) {
		MICROJXL__ASSERT(t == 3);
		microjxl__forward_dct_core(out, in, 3, rep1, 8, microjxl__forward_dct4);
	} else {
		microjxl__forward_dct_core(out, in, t, rep1, 8, microjxl__forward_dct_recur_x8);
	}
}

// this omits the final division by (1 << t)!
MICROJXL_STATIC void microjxl__forward_dct_unscaled(MICROJXL__DCT_ARGS, int32_t rep) {
	if (t <= 0) {
		memcpy(out, in, sizeof(float) * (size_t) rep);
	} else if (rep % 8 == 0) {
		if (t == 1) microjxl__dct2(out, in, 1, rep / 8, 8);
		else if (t == 2) microjxl__forward_dct4(out, in, 2, rep / 8, 8);
		else microjxl__forward_dct_recur_x8(out, in, t, rep / 8, 8);
	} else {
		if (t == 1) microjxl__dct2(out, in, 1, rep, 1);
		else if (t == 2) microjxl__forward_dct4(out, in, 2, rep, 1);
		else microjxl__forward_dct_recur(out, in, t, rep, 1);
	}
}

MICROJXL_ALWAYS_INLINE void microjxl__forward_dct_unscaled_view(microjxl__view_f32 *outv, microjxl__view_f32 *inv) {
	microjxl__adapt_view_f32(outv, inv->logw, inv->logh);
	microjxl__forward_dct_unscaled(outv->ptr, inv->ptr, inv->logh, 1 << inv->logw);
}

MICROJXL_ALWAYS_INLINE void microjxl__inverse_dct4(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2) {
	MICROJXL__ASSERT(t == 2); (void) t;
	microjxl__inverse_dct_core(out, in, 2, rep1, rep2, microjxl__dct2);
}

MICROJXL_STATIC void microjxl__inverse_dct_recur(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2) {
	if (t < 4) {
		MICROJXL__ASSERT(t == 3);
		microjxl__inverse_dct_core(out, in, 3, rep1, rep2, microjxl__inverse_dct4);
	} else {
		microjxl__inverse_dct_core(out, in, t, rep1, rep2, microjxl__inverse_dct_recur);
	}
}

MICROJXL_STATIC void microjxl__inverse_dct_recur_x8(MICROJXL__DCT_ARGS, int32_t rep1, int32_t rep2) {
	MICROJXL__ASSERT(rep2 == 8); (void) rep2;
	if (t < 4) {
		MICROJXL__ASSERT(t == 3);
		microjxl__inverse_dct_core(out, in, 3, rep1, 8, microjxl__inverse_dct4);
	} else {
		microjxl__inverse_dct_core(out, in, t, rep1, 8, microjxl__inverse_dct_recur_x8);
	}
}

MICROJXL_STATIC void microjxl__inverse_dct(MICROJXL__DCT_ARGS, int32_t rep) {
	if (t <= 0) {
		memcpy(out, in, sizeof(float) * (size_t) rep);
	} else if (rep % 8 == 0) {
		if (t == 1) microjxl__dct2(out, in, 1, rep / 8, 8);
		else if (t == 2) microjxl__inverse_dct4(out, in, 2, rep / 8, 8);
		else microjxl__inverse_dct_recur_x8(out, in, t, rep / 8, 8);
	} else {
		if (t == 1) microjxl__dct2(out, in, 1, rep, 1);
		else if (t == 2) microjxl__inverse_dct4(out, in, 2, rep, 1);
		else microjxl__inverse_dct_recur(out, in, t, rep, 1);
	}
}

MICROJXL_ALWAYS_INLINE void microjxl__inverse_dct_view(microjxl__view_f32 *outv, microjxl__view_f32 *inv) {
	microjxl__adapt_view_f32(outv, inv->logw, inv->logh);
	microjxl__inverse_dct(outv->ptr, inv->ptr, inv->logh, 1 << inv->logw);
}

#undef MICROJXL__DCT_ARGS
#undef MICROJXL__IN
#undef MICROJXL__OUT

MICROJXL_STATIC void microjxl__forward_dct2d_scaled_for_llf(
	float *MICROJXL_RESTRICT buf, float *MICROJXL_RESTRICT scratch, int32_t log_rows, int32_t log_columns
) {
	microjxl__view_f32 bufv = microjxl__make_view_f32(log_columns, log_rows, buf);
	microjxl__view_f32 scratchv = microjxl__make_view_f32(log_columns, log_rows, scratch);
	float *p;
	int32_t x, y;

	microjxl__forward_dct_unscaled_view(&scratchv, &bufv);
	microjxl__transpose_view_f32(&bufv, scratchv);
	microjxl__forward_dct_unscaled_view(&scratchv, &bufv);
	// TODO spec bug (I.6.5): the pseudocode only works correctly when C > R;
	// the condition itself can be eliminated by inlining DCT_2D though
	MICROJXL__VIEW_FOREACH(scratchv, y, x, p) {
		// hopefully compiler will factor the second multiplication out of the inner loop (TODO verify this)
		*p *= MICROJXL__LF2LLF_SCALES[(1 << scratchv.logw) + x] * MICROJXL__LF2LLF_SCALES[(1 << scratchv.logh) + y];
	}
	// TODO spec improvement (I.6.3 note): given the pseudocode, it might be better to
	// state that the DCT result *always* has C <= R, transposing as necessary.
	if (log_columns > log_rows) {
		microjxl__transpose_view_f32(&bufv, scratchv);
	} else {
		microjxl__copy_view_f32(&bufv, scratchv);
	}
	MICROJXL__ASSERT(bufv.logw == microjxl__max32(log_columns, log_rows));
	MICROJXL__ASSERT(bufv.logh == microjxl__min32(log_columns, log_rows));
}

MICROJXL_STATIC void microjxl__inverse_dct2d(
	float *MICROJXL_RESTRICT buf, float *MICROJXL_RESTRICT scratch, int32_t log_rows, int32_t log_columns
) {
	microjxl__view_f32 bufv;
	microjxl__view_f32 scratchv = microjxl__make_view_f32(log_columns, log_rows, scratch);

	if (log_columns > log_rows) {
		// TODO spec improvement: coefficients start being transposed, note this as well
		bufv = microjxl__make_view_f32(log_columns, log_rows, buf);
		microjxl__transpose_view_f32(&scratchv, bufv);
	} else {
		bufv = microjxl__make_view_f32(log_rows, log_columns, buf);
		microjxl__copy_view_f32(&scratchv, bufv);
	}
	microjxl__inverse_dct_view(&bufv, &scratchv);
	microjxl__transpose_view_f32(&scratchv, bufv);
	microjxl__inverse_dct_view(&bufv, &scratchv);
	MICROJXL__ASSERT(bufv.logw == log_columns && bufv.logh == log_rows);
}

// a single iteration of AuxIDCT2x2
MICROJXL_ALWAYS_INLINE void microjxl__aux_inverse_dct11(float *out, float *in, int32_t x, int32_t y, int32_t S2) {
	int32_t p = y * 8 + x, q = (y * 2) * 8 + (x * 2);
	float c00 = in[p], c01 = in[p + S2], c10 = in[p + S2 * 8], c11 = in[p + S2 * 9];
	out[q + 000] = c00 + c01 + c10 + c11; // r00
	out[q + 001] = c00 + c01 - c10 - c11; // r01
	out[q + 010] = c00 - c01 + c10 - c11; // r10
	out[q + 011] = c00 - c01 - c10 + c11; // r11
}

MICROJXL_STATIC void microjxl__inverse_dct11(float *buf) {
	float scratch[64];
	int32_t x, y;

	// TODO spec issue: only the "top-left" SxS cells, not "top"
	microjxl__aux_inverse_dct11(buf, buf, 0, 0, 1); // updates buf[(0..1)*8+(0..1)]
	// updates scratch[(0..3)*8+(0..3)], copying other elements from buf in verbatim
	memcpy(scratch, buf, sizeof(float) * 64);
	for (y = 0; y < 2; ++y) for (x = 0; x < 2; ++x) microjxl__aux_inverse_dct11(scratch, buf, x, y, 2);
	// updates the entire buf
	for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x) microjxl__aux_inverse_dct11(buf, scratch, x, y, 4);
}

MICROJXL_STATIC void microjxl__inverse_dct22(float *buf) {
	float scratch[64];
	int32_t x, y;

	microjxl__aux_inverse_dct11(buf, buf, 0, 0, 1);
	// after the top-left inverse DCT2x2, four 4x4 submatrices are formed and IDCTed individually.
	// IDCT itself requires transposition and the final matrices are stitched in a different way,
	// but it turns out that IDCT can be done in place, only requiring the final stitching.
	//
	// input                        after transposition          output
	// a1 a2 b1 b2 c1 c2 d1 d2      a1 a3 e1 e3 i1 i3 m1 m3      a1 e1 i1 m1 a2 e2 i2 m2
	// a3 a4 b3 b4 c3 c4 d3 d4      a2 a4 e2 e4 i2 i4 m2 m4      b1 f1 j1 n1 b2 f2 j2 n2
	// e1 e2 f1 f2 g1 g2 h1 h2      b1 b3 f1 f3 j1 j3 n1 n3      c1 g1 k1 o1 c2 g2 k2 o2
	// e3 e4 f3 f4 g3 g4 h3 h4 ---> b2 b4 f2 f4 j2 j4 n2 n4 ---> d1 k1 l1 p1 d2 k2 l2 p2
	// i1 i2 j1 j2 k1 k2 l1 l2      c1 c3 g1 g3 k1 k3 o1 o3      a3 e3 i3 m3 a4 e4 i4 m4
	// i3 i4 j3 j4 k3 k4 l3 l4      c2 c4 g2 g4 k2 k4 o2 o4      b3 f3 j3 n3 b4 f4 j4 n4
	// m1 m2 n1 n2 o1 o2 p1 p2      d1 d3 h1 h3 l1 l3 p1 p3      c3 g3 k3 o3 c4 g4 k4 o4
	// m3 m4 n3 n4 o3 o4 p3 p4      d2 d4 h2 h4 l2 l4 p2 p4      d3 k3 l3 p3 d4 k4 l4 p4
	//
	// TODO spec issue: notationally `sample` is a *4-dimensional* array, which is not very clear
	microjxl__inverse_dct(scratch, buf, 2, 16); // columnar IDCT for a#-m#, b#-n#, c#-o# and d#-p#
	for (y = 0; y < 8; ++y) for (x = 0; x < 8; ++x) buf[x * 8 + y] = scratch[y * 8 + x];
	microjxl__inverse_dct(scratch, buf, 2, 16); // columnar IDCT for a#-d#, e#-h#, i#-l# and m#-p#
	for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x) {
		buf[y * 8 + x] = scratch[(y * 2) * 8 + (x * 2)];
		buf[y * 8 + (x + 4)] = scratch[(y * 2 + 1) * 8 + (x * 2)];
		buf[(y + 4) * 8 + x] = scratch[(y * 2) * 8 + (x * 2 + 1)];
		buf[(y + 4) * 8 + (x + 4)] = scratch[(y * 2 + 1) * 8 + (x * 2 + 1)];
	}
}

MICROJXL_STATIC void microjxl__inverse_hornuss(float *buf) {
	float scratch[64];
	int32_t x, y, ix, iy;
	memcpy(scratch, buf, sizeof(float) * 64);
	microjxl__aux_inverse_dct11(scratch, buf, 0, 0, 1); // updates scratch[(0..1)*8+(0..1)]
	for (y = 0; y < 2; ++y) for (x = 0; x < 2; ++x) {
		int32_t pos00 = y * 8 + x, pos11 = (y + 2) * 8 + (x + 2);
		float rsum[4] = {0}, sample11;
		for (iy = 0; iy < 4; ++iy) for (ix = 0; ix < 4; ++ix) {
			rsum[ix] += scratch[(y + iy * 2) * 8 + (x + ix * 2)];
		}
		// conceptually (SUM rsum[i]) = residual_sum + coefficients(x, y) in the spec
		sample11 = scratch[pos00] - (rsum[0] + rsum[1] + rsum[2] + rsum[3] - scratch[pos00]) * 0.0625f;
		scratch[pos00] = scratch[pos11];
		scratch[pos11] = 0.0f;
		for (iy = 0; iy < 4; ++iy) for (ix = 0; ix < 4; ++ix) {
			buf[(4 * y + iy) * 8 + (4 * x + ix)] = scratch[(y + iy * 2) * 8 + (x + ix * 2)] + sample11;
		}
	}
}

MICROJXL_STATIC void microjxl__inverse_dct32(float *buf) {
	float scratch[64], tmp;
	microjxl__view_f32 bufv = microjxl__make_view_f32(3, 3, buf);
	microjxl__view_f32 scratchv = microjxl__make_view_f32(3, 3, scratch);

	// coefficients form two 4 rows x 8 columns matrices from even and odd rows;
	// note that this is NOT 8 rows x 4 columns, because of transposition
	// TODO spec issue: inconsistent naming between coeffs_8x4 and coeffs_4x8
	tmp = *MICROJXL__AT(bufv, 0, 0) + *MICROJXL__AT(bufv, 0, 1);
	*MICROJXL__AT(bufv, 0, 1) = *MICROJXL__AT(bufv, 0, 0) - *MICROJXL__AT(bufv, 0, 1);
	*MICROJXL__AT(bufv, 0, 0) = tmp;
	microjxl__reshape_view_f32(&bufv, 4, 2);
	microjxl__inverse_dct_view(&scratchv, &bufv);
	microjxl__reshape_view_f32(&scratchv, 3, 3);
	microjxl__transpose_view_f32(&bufv, scratchv);
	microjxl__inverse_dct_view(&scratchv, &bufv);
	microjxl__oddeven_columns_to_halves_f32(&bufv, scratchv);
	MICROJXL__ASSERT(bufv.logw == 3 && bufv.logh == 3);
}

MICROJXL_STATIC void microjxl__inverse_dct23(float *buf) {
	float scratch[64];
	microjxl__view_f32 bufv = microjxl__make_view_f32(3, 3, buf);
	microjxl__view_f32 scratchv = microjxl__make_view_f32(3, 3, scratch);

	// coefficients form two 4 rows x 8 columns matrices from even and odd rows
	microjxl__copy_view_f32(&scratchv, bufv);
	*MICROJXL__AT(scratchv, 0, 0) = *MICROJXL__AT(bufv, 0, 0) + *MICROJXL__AT(bufv, 0, 1);
	*MICROJXL__AT(scratchv, 0, 1) = *MICROJXL__AT(bufv, 0, 0) - *MICROJXL__AT(bufv, 0, 1);
	microjxl__transpose_view_f32(&bufv, scratchv);
	microjxl__inverse_dct_view(&scratchv, &bufv);
	microjxl__transpose_view_f32(&bufv, scratchv);
	microjxl__reshape_view_f32(&bufv, 4, 2);
	microjxl__inverse_dct_view(&scratchv, &bufv);
	microjxl__reshape_view_f32(&scratchv, 3, 3);
	microjxl__oddeven_rows_to_halves_f32(&bufv, scratchv);
	MICROJXL__ASSERT(bufv.logw == 3 && bufv.logh == 3);
}

// TODO spec issue: the input is a 4x4 matrix but indexed like a 1-dimensional array
MICROJXL_STATIC void microjxl__inverse_afv22(float *MICROJXL_RESTRICT out, float *MICROJXL_RESTRICT in) {
	static const float AFV_BASIS[256] = { // AFVBasis in the specification, but transposed
		 0.25000000f,  0.87690293f,  0.00000000f,  0.00000000f,
		 0.00000000f, -0.41053776f,  0.00000000f,  0.00000000f,
		 0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f,
		 0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f,
		 0.25000000f,  0.22065181f,  0.00000000f,  0.00000000f,
		-0.70710678f,  0.62354854f,  0.00000000f,  0.00000000f,
		 0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f,
		 0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f,
		 0.25000000f, -0.10140050f,  0.40670076f, -0.21255748f,
		 0.00000000f, -0.06435072f, -0.45175566f, -0.30468475f,
		 0.30179295f,  0.40824829f,  0.17478670f, -0.21105601f,
		-0.14266085f, -0.13813540f, -0.17437603f,  0.11354987f,
		 0.25000000f, -0.10140050f,  0.44444817f,  0.30854971f,
		 0.00000000f, -0.06435072f,  0.15854504f,  0.51126161f,
		 0.25792363f,  0.00000000f,  0.08126112f,  0.18567181f,
		-0.34164468f,  0.33022826f,  0.07027907f, -0.07417505f,
		 0.25000000f,  0.22065181f,  0.00000000f,  0.00000000f,
		 0.70710678f,  0.62354854f,  0.00000000f,  0.00000000f,
		 0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f,
		 0.00000000f,  0.00000000f,  0.00000000f,  0.00000000f,
		 0.25000000f, -0.10140050f,  0.00000000f,  0.47067023f,
		 0.00000000f, -0.06435072f, -0.04038515f,  0.00000000f,
		 0.16272340f,  0.00000000f,  0.00000000f,  0.00000000f,
		 0.73674975f,  0.08755115f, -0.29210266f,  0.19402893f,
		 0.25000000f, -0.10140050f,  0.19574399f, -0.16212052f,
		 0.00000000f, -0.06435072f,  0.00741823f, -0.29048013f,
		 0.09520023f,  0.00000000f, -0.36753980f,  0.49215859f,
		 0.24627108f, -0.07946707f,  0.36238173f, -0.43519050f,
		 0.25000000f, -0.10140050f,  0.29291001f,  0.00000000f,
		 0.00000000f, -0.06435072f,  0.39351034f, -0.06578702f,
		 0.00000000f, -0.40824829f, -0.30788221f, -0.38525014f,
		-0.08574019f, -0.46133749f,  0.00000000f,  0.21918685f,
		 0.25000000f, -0.10140050f, -0.40670076f, -0.21255748f,
		 0.00000000f, -0.06435072f, -0.45175566f,  0.30468475f,
		 0.30179295f, -0.40824829f, -0.17478670f,  0.21105601f,
		-0.14266085f, -0.13813540f, -0.17437603f,  0.11354987f,
		 0.25000000f, -0.10140050f, -0.19574399f, -0.16212052f,
		 0.00000000f, -0.06435072f,  0.00741823f,  0.29048013f,
		 0.09520023f,  0.00000000f,  0.36753980f, -0.49215859f,
		 0.24627108f, -0.07946707f,  0.36238173f, -0.43519050f,
		 0.25000000f, -0.10140050f,  0.00000000f, -0.47067023f,
		 0.00000000f, -0.06435072f,  0.11074166f,  0.00000000f,
		-0.16272340f,  0.00000000f,  0.00000000f,  0.00000000f,
		 0.14883399f,  0.49724647f,  0.29210266f,  0.55504438f,
		 0.25000000f, -0.10140050f,  0.11379074f, -0.14642919f,
		 0.00000000f, -0.06435072f,  0.08298163f, -0.23889774f,
		-0.35312385f, -0.40824829f,  0.48266891f,  0.17419413f,
		-0.04768680f,  0.12538059f, -0.43266080f, -0.25468277f,
		 0.25000000f, -0.10140050f, -0.44444817f,  0.30854971f,
		 0.00000000f, -0.06435072f,  0.15854504f, -0.51126161f,
		 0.25792363f,  0.00000000f, -0.08126112f, -0.18567181f,
		-0.34164468f,  0.33022826f,  0.07027907f, -0.07417505f,
		 0.25000000f, -0.10140050f, -0.29291001f,  0.00000000f,
		 0.00000000f, -0.06435072f,  0.39351034f,  0.06578702f,
		 0.00000000f,  0.40824829f,  0.30788221f,  0.38525014f,
		-0.08574019f, -0.46133749f,  0.00000000f,  0.21918685f,
		 0.25000000f, -0.10140050f, -0.11379074f, -0.14642919f,
		 0.00000000f, -0.06435072f,  0.08298163f,  0.23889774f,
		-0.35312385f,  0.40824829f, -0.48266891f, -0.17419413f,
		-0.04768680f,  0.12538059f, -0.43266080f, -0.25468277f,
		 0.25000000f, -0.10140050f,  0.00000000f,  0.42511496f,
		 0.00000000f, -0.06435072f, -0.45175566f,  0.00000000f,
		-0.60358590f,  0.00000000f,  0.00000000f,  0.00000000f,
		-0.14266085f, -0.13813540f,  0.34875205f,  0.11354987f,
	};

	int32_t i, j;
	for (i = 0; i < 16; ++i) {
		float sum = 0.0f;
		for (j = 0; j < 16; ++j) sum += in[j] * AFV_BASIS[i * 16 + j];
		out[i] = sum;
	}
}

MICROJXL_STATIC void microjxl__inverse_afv(float *buf, int flipx, int flipy) {
	// input          flipx/y=0/0     flipx/y=1/0     flipx/y=0/1     flipx/y=1/1
	//  _______       +-----+-----+   +-----+-----+   +-----------+   +-----------+
	// |_|_|_|_|      |'    |     |   |     |    '|   |           |   |           |
	// |_|_|_|_| ---> |AFV22|DCT22|   |DCT22|AFV22|   |   DCT23   |   |   DCT23   |
	// |_|_|_|_|      +-----+-----+   +-----+-----+   +-----+-----+   +-----+-----+
	// |_|_|_|_|      |   DCT23   |   |   DCT23   |   |AFV22|DCT22|   |DCT22|AFV22|
	//                |           |   |           |   |.    |     |   |     |    .|
	// (2x2 each)     +-----------+   +-----------+   +-----+-----+   +-----+-----+
	//
	// coefficients are divided by 16 2x2 blocks, where two top coefficients are for AFV22
	// and DCT22 respectively and two bottom coefficients are for DCT23.
	// all three corresponding DC coefficients are in the top-left block and handled specially.
	// AFV22 samples are then flipped so that the top-left cell is moved to the corner (dots above).
	//
	// TODO spec issue: identifiers have `*` in place of `x`

	float scratch[64];
	// buf23/buf32 etc. refer to the same memory region; numbers refer to the supposed dimensions
	float *bufafv = buf, *buf22 = buf + 16, *buf23 = buf + 32, *buf32 = buf23;
	float *scratchafv = scratch, *scratch22 = scratch + 16, *scratch23 = scratch + 32, *scratch32 = scratch23;
	int32_t x, y;

	MICROJXL__ASSERT(flipx == !!flipx && flipy == !!flipy);

	for (y = 0; y < 8; y += 2) for (x = 0; x < 8; ++x) {
		// AFV22 coefficients to scratch[0..16), DCT22 coefficients to scratch[16..32)
		scratch[(x % 2) * 16 + (y / 2) * 4 + (x / 2)] = buf[y * 8 + x];
	}
	for (y = 1; y < 8; y += 2) for (x = 0; x < 8; ++x) {
		// DCT23 coefficients to scratch[32..64) = scratch32[0..32), after transposition
		scratch32[x * 4 + (y / 2)] = buf[y * 8 + x];
	}
	scratchafv[0] = (buf[0] + buf[1] + buf[8]) * 4.0f;
	scratch22[0] = buf[0] - buf[1] + buf[8]; // TODO spec bug: x and y are swapped
	scratch32[0] = buf[0] - buf[8]; // TODO spec bug: x and y are swapped

	microjxl__inverse_afv22(bufafv, scratchafv);
	microjxl__inverse_dct(buf22, scratch22, 2, 4);
	microjxl__inverse_dct(buf32, scratch32, 3, 4);

	for (y = 0; y < 4; ++y) {
		for (x = 0; x < 4; ++x) scratchafv[y * 4 + x] = bufafv[y * 4 + x]; // AFV22, as is
		for (x = 0; x < 4; ++x) scratch22[x * 4 + y] = buf22[y * 4 + x]; // DCT22, transposed
	}
	for (y = 0; y < 8; ++y) {
		for (x = 0; x < 4; ++x) scratch23[x * 8 + y] = buf32[y * 4 + x]; // DCT23, transposed
	}

	microjxl__inverse_dct(buf22, scratch22, 2, 4);
	microjxl__inverse_dct(buf23, scratch23, 2, 8);
	memcpy(scratch + 16, buf + 16, sizeof(float) * 48);

	for (y = 0; y < 4; ++y) {
		static const int8_t FLIP_FOR_AFV[2][4] = {{0, 1, 2, 3}, {7, 6, 5, 4}};
		int32_t afv22pos = FLIP_FOR_AFV[flipy][y] * 8;
		int32_t dct22pos = (flipy * 4 + y) * 8 + (!flipx * 4);
		int32_t dct23pos = (!flipy * 4 + y) * 8;
		for (x = 0; x < 4; ++x) buf[afv22pos + FLIP_FOR_AFV[flipx][x]] = scratchafv[y * 4 + x];
		for (x = 0; x < 4; ++x) buf[dct22pos + x] = scratch22[y * 4 + x];
		// TODO spec issue: samples_4x4 should be samples_4x8
		for (x = 0; x < 8; ++x) buf[dct23pos + x] = scratch23[y * 8 + x];
	}
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// LfGlobal: additional image features, HF block context, global tree, extra channels

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_global(microjxl__st *st);

////////////////////////////////////////////////////////////////////////////////
// patches (K.3): dictionary decoding, reference-frame save, and application

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_STATIC void microjxl__mem_free_patch_dict(microjxl__patch_dict *pd) {
	microjxl__mem_free(pd->ref_idx);
	microjxl__mem_free(pd->ref_x0);
	microjxl__mem_free(pd->ref_y0);
	microjxl__mem_free(pd->ref_xs);
	microjxl__mem_free(pd->ref_ys);
	microjxl__mem_free(pd->pos_ref);
	microjxl__mem_free(pd->pos_x);
	microjxl__mem_free(pd->pos_y);
	microjxl__mem_free(pd->blend);
	microjxl__mem_free_code_spec(&pd->codespec);
	pd->ref_idx = pd->ref_x0 = pd->ref_y0 = pd->ref_xs = pd->ref_ys = NULL;
	pd->pos_ref = pd->pos_x = pd->pos_y = NULL;
	pd->blend = NULL;
	pd->num_ref = pd->num_pos = 0;
	pd->codespec_valid = 0;
}

/* K.3.2 PatchDictionary (libjxl dec_patch_dictionary.cc PatchDictionary::Decode).
 * Ten hybrid-uint contexts: {0 num_ref_patch, 1 ref frame, 2 patch size,
 * 3 ref position, 4 patch position, 5 blend mode, 6 offset, 7 count,
 * 8 alpha channel, 9 clamp}. Position limits use the frame's stored
 * (pre-upsampling) dimensions, exactly like libjxl's frame_dim.*size_padded
 * for the frames in the wild. */
MICROJXL_INLINE int32_t microjxl__maxpixel_scale(const microjxl__image_st *im); // defined in the render section
MICROJXL__STATIC_RETURNS_ERR microjxl__decode_patch_dict(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	microjxl__patch_dict *pd = &f->patches;
	microjxl__code_st code = MICROJXL__INIT;
	int64_t num_pixels = (int64_t) f->width * (int64_t) f->height;
	int64_t max_ref_patches = 1024 + num_pixels / 4;
	int64_t max_patches = max_ref_patches * 4;
	int64_t max_blending_infos = max_patches * 4;
	int64_t total_patches = 0;
	/* each growing array gets its own capacity counter: microjxl__realloc64
	 * returns the ORIGINAL pointer when no growth is needed, which is NULL
	 * for the not-yet-allocated arrays -- sharing one counter would leave
	 * the later arrays NULL (and the TRY macro would silently "succeed"
	 * because no error flag is set on that path) */
	int64_t ncap = 0, xcap = 0, ycap = 0, bcap = 0;
	int64_t id, ri;
	int32_t j;
	int32_t num_ec = im->num_extra_channels;

	/* re-entry safety: LfGlobal restarts from the top when the section
	 * buffer runs out, so any partial dictionary must be released */
	microjxl__mem_free_patch_dict(pd);
	pd->blendings_stride = num_ec + 1;

	MICROJXL__TRY(microjxl__read_code_spec(st, 10, &pd->codespec));
	microjxl__init_code(&code, &pd->codespec);
	pd->codespec_valid = 1;

	{
		int64_t num_ref_patch = (int64_t) (uint32_t) microjxl__code(st, 0, 0, &code);
		MICROJXL__RAISE_DELAYED();
		MICROJXL__SHOULD(num_ref_patch >= 0 && num_ref_patch <= max_ref_patches, "plim");
		pd->num_ref = num_ref_patch;
		/* per-reference geometry, filled below; apply sites index these by
		 * pos_ref[pi] == ref index */
		if (num_ref_patch > 0) {
			MICROJXL__TRY_MALLOC(int32_t, &pd->ref_idx, (size_t) num_ref_patch);
			MICROJXL__TRY_MALLOC(int32_t, &pd->ref_x0, (size_t) num_ref_patch);
			MICROJXL__TRY_MALLOC(int32_t, &pd->ref_y0, (size_t) num_ref_patch);
			MICROJXL__TRY_MALLOC(int32_t, &pd->ref_xs, (size_t) num_ref_patch);
			MICROJXL__TRY_MALLOC(int32_t, &pd->ref_ys, (size_t) num_ref_patch);
		}
	}

	for (id = 0; id < pd->num_ref; ++id) {
		int32_t ref = (int32_t) (uint32_t) microjxl__code(st, 1, 0, &code);
		int64_t x0, y0, xs, ys, id_count;
		MICROJXL__RAISE_DELAYED();
		MICROJXL__SHOULD(ref >= 0 && ref < 4, "pats");
		MICROJXL__SHOULD(im->ref_present[ref] && im->ref_nch[ref] > 0, "pats");
		MICROJXL__SHOULD(im->ref_in_xyb[ref], "pats");
		x0 = (int64_t) (uint32_t) microjxl__code(st, 3, 0, &code);
		MICROJXL__RAISE_DELAYED();
		y0 = (int64_t) (uint32_t) microjxl__code(st, 3, 0, &code);
		MICROJXL__RAISE_DELAYED();
		xs = (int64_t) (uint32_t) microjxl__code(st, 2, 0, &code) + 1;
		MICROJXL__RAISE_DELAYED();
		ys = (int64_t) (uint32_t) microjxl__code(st, 2, 0, &code) + 1;
		MICROJXL__RAISE_DELAYED();
		MICROJXL__SHOULD(x0 + xs <= im->ref_w[ref] && y0 + ys <= im->ref_h[ref], "pats");
		pd->ref_idx[id] = ref;
		pd->ref_x0[id] = (int32_t) x0;
		pd->ref_y0[id] = (int32_t) y0;
		pd->ref_xs[id] = (int32_t) xs;
		pd->ref_ys[id] = (int32_t) ys;
		id_count = (int64_t) (uint32_t) microjxl__code(st, 7, 0, &code) + 1;
		MICROJXL__RAISE_DELAYED();
		total_patches += id_count;
		MICROJXL__SHOULD(id_count <= max_patches && total_patches <= max_patches, "pats");
		MICROJXL__SHOULD(total_patches * pd->blendings_stride <= max_blending_infos, "pats");
		/* grow position/blend arrays (simple doubling; sizes are bounded so
		 * int32 indices are fine) */
		if (total_patches > ncap) {
			int64_t newcap = ncap ? ncap * 2 : total_patches;
			while (newcap < total_patches) newcap *= 2;
			MICROJXL__TRY_REALLOC64(int32_t, &pd->pos_ref, newcap, &ncap);
			MICROJXL__TRY_REALLOC64(int32_t, &pd->pos_x, newcap, &xcap);
			MICROJXL__TRY_REALLOC64(int32_t, &pd->pos_y, newcap, &ycap);
			{
				int64_t bncap = newcap * pd->blendings_stride;
				MICROJXL__TRY_REALLOC64(microjxl__patch_blend, &pd->blend, bncap, &bcap);
			}
		}
		for (ri = 0; ri < id_count; ++ri) {
			int64_t pi = pd->num_pos;
			int64_t px, py;
			if (ri == 0) {
				px = (int64_t) (uint32_t) microjxl__code(st, 4, 0, &code);
				MICROJXL__RAISE_DELAYED();
				py = (int64_t) (uint32_t) microjxl__code(st, 4, 0, &code);
				MICROJXL__RAISE_DELAYED();
			} else {
				int64_t deltax = microjxl__unpack_signed64((int64_t) (uint32_t) microjxl__code(st, 6, 0, &code));
				MICROJXL__RAISE_DELAYED();
				int64_t deltay = microjxl__unpack_signed64((int64_t) (uint32_t) microjxl__code(st, 6, 0, &code));
				MICROJXL__RAISE_DELAYED();
				px = pd->pos_x[pi - 1] + deltax;
				py = pd->pos_y[pi - 1] + deltay;
				MICROJXL__SHOULD(!(deltax < 0 && -deltax > (int64_t) pd->pos_x[pi - 1]), "pats");
				MICROJXL__SHOULD(!(deltay < 0 && -deltay > (int64_t) pd->pos_y[pi - 1]), "pats");
			}
			MICROJXL__SHOULD(px + xs <= f->width && py + ys <= f->height, "pats");
			pd->pos_ref[pi] = (int32_t) id;
			pd->pos_x[pi] = (int32_t) px;
			pd->pos_y[pi] = (int32_t) py;
			for (j = 0; j < pd->blendings_stride; ++j) {
				microjxl__patch_blend *bl = &pd->blend[pi * pd->blendings_stride + j];
				int32_t mode = (int32_t) (uint32_t) microjxl__code(st, 5, 0, &code);
				MICROJXL__RAISE_DELAYED();
				MICROJXL__SHOULD(mode >= 0 && mode < 8, "pats");
				bl->mode = mode;
				bl->alpha_channel = 0;
				bl->clamp = 0;
				if ((mode == 4 || mode == 5 || mode == 6 || mode == 7) && num_ec > 1) {
					int32_t ac = (int32_t) (uint32_t) microjxl__code(st, 8, 0, &code);
					MICROJXL__RAISE_DELAYED();
					MICROJXL__SHOULD(ac >= 0 && ac < num_ec, "pats");
					bl->alpha_channel = ac;
				}
				if (mode == 3 || mode == 4 || mode == 5 || mode == 6 || mode == 7) {
					bl->clamp = (int32_t) (uint32_t) microjxl__code(st, 9, 0, &code);
					MICROJXL__RAISE_DELAYED();
				}
			}
			pd->num_pos = pi + 1;
		}
	}
	MICROJXL__TRY(microjxl__finish_and_free_code(st, &code));
	microjxl__mem_free_code_spec(&pd->codespec);
	pd->codespec_valid = 0;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_code(&code);
	return st->err;
}

/* Blend one row segment (libjxl blending.cc PerformBlending): extra
 * channels first (so colour blending sees the pre-blending alpha), then
 * the three colour channels. bg = frame rows, fg = reference patch rows,
 * out == bg. All pointers are full-row bases; x0 is the segment offset
 * within those rows and xs the segment length. Blend modes 4/5 write the
 * colour alpha too (the reference patch's alpha plane is blended into
 * the frame's alpha) when the frame has an alpha extra channel. */
MICROJXL__STATIC_RETURNS_ERR microjxl__patch_blend_row(
	microjxl__st *st, microjxl__plane *bgp /* [3 + num_ec] */, const microjxl__plane *fgp /* [3 + num_ec] */,
	int32_t y, int32_t x0, int32_t xs, int32_t fy, int32_t fx0,
	const microjxl__patch_blend *color_bl, const microjxl__patch_blend *ec_bl,
	int num_ec, const microjxl__ec_info *ec_info) {
	/* y/x0 address the background (frame) rows; fy/fx0 address the
	 * foreground (reference) rows so the patch's ref origin is honoured
	 * (libjxl AddOneRow: ref_pos.y0 + iy / ref_pos.x0 + x0 - bx). */
	int has_alpha = 0, alpha_c = 0, i, x;
	float *tmp = NULL;
	for (i = 0; i < num_ec; ++i) {
		if (ec_info[i].type == MICROJXL__EC_ALPHA) { has_alpha = 1; alpha_c = i; break; }
	}
	MICROJXL__TRY_MALLOC(float, &tmp, (size_t) (3 + num_ec) * (size_t) xs);

#define MICROJXL__CLAMP01(v) ((v) < 0.0f ? 0.0f : (v) > 1.0f ? 1.0f : (v))
#define MICROJXL__FA(fgp_, ac_, xx, cl) \
	((cl) ? MICROJXL__CLAMP01(MICROJXL__F32_PIXELS(&(fgp_)[3 + (ac_)], fy)[fx0 + (xx)]) : \
		MICROJXL__F32_PIXELS(&(fgp_)[3 + (ac_)], fy)[fx0 + (xx)])

	/* extra channels first (pre-blending alpha) */
	for (i = 0; i < num_ec; ++i) {
		const microjxl__patch_blend *bl = &ec_bl[i];
		int m = bl->mode;
		float *dst = tmp + (size_t) (3 + i) * (size_t) xs;
		const float *brow = MICROJXL__F32_PIXELS(&bgp[3 + i], y);
		const float *frow = MICROJXL__F32_PIXELS(&fgp[3 + i], fy);
		int ac = bl->alpha_channel;
		if (m == 0) { // kNone: keep background
			for (x = 0; x < xs; ++x) dst[x] = brow[x0 + x];
		} else if (m == 1) { // kReplace
			for (x = 0; x < xs; ++x) dst[x] = frow[fx0 + x];
		} else if (m == 2) { // kAdd
			for (x = 0; x < xs; ++x) dst[x] = brow[x0 + x] + frow[fx0 + x];
		} else if (m == 3) { // kMul
			for (x = 0; x < xs; ++x) {
				dst[x] = brow[x0 + x] * (bl->clamp ? MICROJXL__CLAMP01(frow[fx0 + x]) : frow[fx0 + x]);
			}
		} else if (m == 4 || m == 5) { // kBlendAbove / kBlendBelow
			/* PerformAlphaBlending(bg, bga, fg, fga): above = (bg,fg),
			 * below swaps the layers (blending.cc). */
			const float *vbg = brow, *vfg = frow;
			const microjxl__plane *abgp = bgp, *afgp = fgp;
			if (m == 5) {
				vbg = frow; vfg = brow;
				abgp = fgp; afgp = bgp; // alpha planes swap too
			}
			int premult = ec_info[ac].data.alpha_associated;
			for (x = 0; x < xs; ++x) {
				float fa = MICROJXL__FA(afgp, ac, x, bl->clamp);
				float bga = MICROJXL__F32_PIXELS(&abgp[3 + ac], y)[x0 + x];
				if (premult) {
					dst[x] = vfg[x0 + x] + vbg[x0 + x] * (1.0f - fa);
				} else {
					float na = 1.0f - (1.0f - fa) * (1.0f - bga);
					float rna = na > 0.0f ? 1.0f / na : 0.0f;
					dst[x] = (vfg[x0 + x] * fa + vbg[x0 + x] * bga * (1.0f - fa)) * rna;
				}
			}
		} else if (m == 6 || m == 7) { // kAlphaWeightedAddAbove / Below
			const float *vbg = brow, *vfg = frow, *wa;
			if (m == 7) { const float *sw = vbg; vbg = frow; vfg = sw; }
			wa = (m == 6) ? MICROJXL__F32_PIXELS(&fgp[3 + ac], fy) : MICROJXL__F32_PIXELS(&bgp[3 + ac], y);
			for (x = 0; x < xs; ++x) {
				float w = bl->clamp ? MICROJXL__CLAMP01(wa[x0 + x]) : wa[x0 + x];
				dst[x] = vbg[x0 + x] + vfg[x0 + x] * w;
			}
		}
	}
	/* colour channels */
	{
		int m = color_bl->mode;
		int ac = color_bl->alpha_channel;
		for (i = 0; i < 3; ++i) {
			float *dst = tmp + (size_t) i * (size_t) xs;
			const float *brow = MICROJXL__F32_PIXELS(&bgp[i], y);
			const float *frow = MICROJXL__F32_PIXELS(&fgp[i], fy);
			if (m == 0) {
				for (x = 0; x < xs; ++x) dst[x] = brow[x0 + x];
			} else if (m == 1) {
				for (x = 0; x < xs; ++x) dst[x] = frow[fx0 + x];
			} else if (m == 2) {
				for (x = 0; x < xs; ++x) dst[x] = brow[x0 + x] + frow[fx0 + x];
			} else if (m == 6) { // kAlphaWeightedAddAbove: bg + fg*fa
				if (has_alpha) {
					const float *wa = MICROJXL__F32_PIXELS(&fgp[3 + ac], fy);
					for (x = 0; x < xs; ++x) {
						float w = color_bl->clamp ? MICROJXL__CLAMP01(wa[x0 + x]) : wa[x0 + x];
						dst[x] = brow[x0 + x] + frow[fx0 + x] * w;
					}
				} else {
					for (x = 0; x < xs; ++x) dst[x] = brow[x0 + x] + frow[fx0 + x];
				}
			} else if (m == 7) { // kAlphaWeightedAddBelow: fg + bg*bga
				if (has_alpha) {
					const float *wa = MICROJXL__F32_PIXELS(&bgp[3 + ac], y);
					for (x = 0; x < xs; ++x) {
						float w = color_bl->clamp ? MICROJXL__CLAMP01(wa[x0 + x]) : wa[x0 + x];
						dst[x] = frow[fx0 + x] + brow[x0 + x] * w;
					}
				} else {
					for (x = 0; x < xs; ++x) dst[x] = brow[x0 + x] + frow[fx0 + x];
				}
			} else if (m == 4) { // kBlendAbove
				if (has_alpha) {
					int premult = ec_info[ac].data.alpha_associated;
					for (x = 0; x < xs; ++x) {
						float fa = MICROJXL__FA(fgp, ac, x, color_bl->clamp);
						float bga = MICROJXL__F32_PIXELS(&bgp[3 + ac], y)[x0 + x];
						if (premult) {
							dst[x] = frow[fx0 + x] + brow[x0 + x] * (1.0f - fa);
						} else {
							float na = 1.0f - (1.0f - fa) * (1.0f - bga);
							float rna = na > 0.0f ? 1.0f / na : 0.0f;
							dst[x] = (frow[fx0 + x] * fa + brow[x0 + x] * bga * (1.0f - fa)) * rna;
						}
					}
				} else {
					for (x = 0; x < xs; ++x) dst[x] = frow[fx0 + x];
				}
			} else if (m == 5) { // kBlendBelow: layers swap (fg is "bottom")
				if (has_alpha) {
					int premult = ec_info[ac].data.alpha_associated;
					for (x = 0; x < xs; ++x) {
						float fa = MICROJXL__FA(bgp, ac, x, color_bl->clamp);
						float fga = MICROJXL__F32_PIXELS(&fgp[3 + ac], fy)[fx0 + x];
						if (premult) {
							dst[x] = brow[x0 + x] + frow[fx0 + x] * (1.0f - fa);
						} else {
							float na = 1.0f - (1.0f - fa) * (1.0f - fga);
							float rna = na > 0.0f ? 1.0f / na : 0.0f;
							dst[x] = (brow[x0 + x] * fa + frow[fx0 + x] * fga * (1.0f - fa)) * rna;
						}
					}
				} else {
					for (x = 0; x < xs; ++x) dst[x] = frow[fx0 + x];
				}
			} else { // m == 3: kMul
				for (x = 0; x < xs; ++x) {
					dst[x] = brow[x0 + x] * (color_bl->clamp ? MICROJXL__CLAMP01(frow[fx0 + x]) : frow[fx0 + x]);
				}
			}
		}
	}
	/* colour kBlend (modes 4/5) also blends the alpha channel itself
	 * (libjxl blend_weighted writes tmp.Row(3 + alpha)) */
	if ((color_bl->mode == 4 || color_bl->mode == 5) && has_alpha) {
		int m = color_bl->mode;
		float *dst = tmp + (size_t) (3 + alpha_c) * (size_t) xs;
		const float *brow = MICROJXL__F32_PIXELS(&bgp[3 + alpha_c], y);
		const float *frow = MICROJXL__F32_PIXELS(&fgp[3 + alpha_c], fy);
		for (x = 0; x < xs; ++x) {
			float fa, bga;
			if (m == 4) {
				fa = MICROJXL__FA(fgp, alpha_c, x, color_bl->clamp);
				bga = brow[x0 + x];
			} else {
				fa = MICROJXL__FA(bgp, alpha_c, x, color_bl->clamp);
				bga = frow[fx0 + x];
			}
			dst[x] = 1.0f - (1.0f - fa) * (1.0f - bga);
		}
	}
	/* copy back */
	for (i = 0; i < 3 + num_ec; ++i) {
		float *brow = MICROJXL__F32_PIXELS(&bgp[i], y);
		const float *src = tmp + (size_t) i * (size_t) xs;
		for (x = 0; x < xs; ++x) brow[x0 + x] = src[x];
	}
	microjxl__mem_free(tmp);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free(tmp);
	return st->err;
#undef MICROJXL__FA
#undef MICROJXL__CLAMP01
}

/* Apply all patches overlapping row y of the current frame.
 * bgp points at the frame's 3+num_ec float planes; the reference rows
 * come from im->ref_planes[ref]. Only used for xyb-encoded frames whose
 * patches reference in-xyb refs (validated at decode). */
MICROJXL__STATIC_RETURNS_ERR microjxl__apply_patches_row(
	microjxl__st *st, microjxl__plane *bgp, int32_t y) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	microjxl__patch_dict *pd = &f->patches;
	int64_t pi;
	int32_t num_ec = im->num_extra_channels;
	if (pd->num_pos == 0) return 0;
	for (pi = 0; pi < pd->num_pos; ++pi) {
		int32_t ri = pd->pos_ref[pi];
		int32_t bx = pd->pos_x[pi], by = pd->pos_y[pi];
		int32_t rx0 = pd->ref_x0[ri], ry0 = pd->ref_y0[ri];
		int32_t xs = pd->ref_xs[ri], ys = pd->ref_ys[ri];
		int32_t seg_x0, seg_x1;
		const microjxl__patch_blend *bls;
		/* ri indexes the dictionary's reference list; the frame slot is ref_idx */
		const microjxl__plane *fgp = im->ref_planes[pd->ref_idx[ri]];
		if (y < by || y >= by + ys) continue;
		if (bx >= f->width || bx + xs <= 0) continue;
		seg_x0 = bx > 0 ? bx : 0;
		seg_x1 = bx + xs < f->width ? bx + xs : f->width;
		if (seg_x1 <= seg_x0) continue;
		bls = &pd->blend[pi * pd->blendings_stride];
		MICROJXL__TRY(microjxl__patch_blend_row(st, bgp, fgp,
			y, seg_x0, seg_x1 - seg_x0,
			y - by + ry0, rx0 + (seg_x0 - bx),
			&bls[0], &bls[1], num_ec, im->ec_info));
	}
	return 0;
MICROJXL__ON_ERROR:
	return st->err;
}

/* single-channel patch blend step (blending.cc / alpha.cc); the caller
 * supplies fa/bga with the exact clamping/swapping semantics the row
 * version implements: modes 4/5 (kBlendAbove/Below) swap roles, fa is
 * clamped per the blend flag, bga never is. */
MICROJXL_STATIC float microjxl__patch_blend_one(
	int m, float bg, float fg, float fa, float bga, int premult
) {
	switch (m) {
	case 0: return bg; /* kNone */
	case 1: return fg; /* kReplace */
	case 2: return bg + fg; /* kAdd */
	case 3: return bg * fg; /* kMul (fg pre-clamped by caller) */
	case 4: /* kBlendAbove */
		if (premult) return fg + bg * (1.0f - fa);
		else {
			float na = 1.0f - (1.0f - fa) * (1.0f - bga);
			return na > 0.0f ? (fg * fa + bg * bga * (1.0f - fa)) / na : 0.0f;
		}
	case 5: /* kBlendBelow: layers swapped */
		if (premult) return bg + fg * (1.0f - fa);
		else {
			float na = 1.0f - (1.0f - fa) * (1.0f - bga);
			return na > 0.0f ? (bg * fa + fg * bga * (1.0f - fa)) / na : 0.0f;
		}
	case 6: return bg + fg * fa; /* kAlphaWeightedAddAbove */
	case 7: return fg + bg * bga; /* kAlphaWeightedAddBelow */
	}
	return bg;
}

MICROJXL_STATIC float microjxl__patch_clamp01(float v) {
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/* VarDCT per-pixel patch application (combine site): sequentially blends
 * every patch position covering (gx, gy) over the running correlated-XYB
 * pixel and the frame's extra channels (per-pixel formulation of
 * microjxl__patch_blend_row; libjxl stage_patches). Only xyb frames reach here
 * (patch dictionary decode rejects non-xyb refs). EC blending is capped
 * at 16 channels (patches with more ECs are not known to exist). */
MICROJXL__STATIC_RETURNS_ERR microjxl__apply_patches_pixel(
	microjxl__st *st, int32_t gx, int32_t gy, float *v /* [3]: correlated X, Y, B */
) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	microjxl__patch_dict *pd = &f->patches;
	int32_t num_ec = im->num_extra_channels;
	int32_t has_alpha = 0, alpha_c = 0;
	int32_t maxpixel = microjxl__maxpixel_scale(im);
	int64_t pi;
	int32_t i;
	if (pd->num_pos == 0) return 0;
	for (i = 0; i < num_ec; ++i) {
		if (im->ec_info[i].type == MICROJXL__EC_ALPHA) { has_alpha = 1; alpha_c = i; break; }
	}
	for (pi = 0; pi < pd->num_pos; ++pi) {
		int32_t ri = pd->pos_ref[pi];
		int32_t bx = pd->pos_x[pi], by = pd->pos_y[pi];
		int32_t rx0 = pd->ref_x0[ri], ry0 = pd->ref_y0[ri];
		int32_t xs = pd->ref_xs[ri], ys = pd->ref_ys[ri];
		int32_t rx, ry;
		const microjxl__patch_blend *bls;
		/* ri indexes the dictionary's reference list; the frame slot is ref_idx */
		const microjxl__plane *rfp = im->ref_planes[pd->ref_idx[ri]];
		float fgc[3], newc[3];
		float bgec[16];
		int32_t nec = num_ec < 16 ? num_ec : 16;
		if (gx < bx || gx >= bx + xs || gy < by || gy >= by + ys) continue;
		rx = rx0 + (gx - bx); ry = ry0 + (gy - by);
		bls = &pd->blend[pi * pd->blendings_stride];
		for (i = 0; i < 3; ++i) fgc[i] = MICROJXL__F32_PIXELS(&rfp[i], ry)[rx];
		/* snapshot the frame's pre-blend EC values (the colour pass reads
		 * pre-blending alpha, libjxl PerformBlending) */
		for (i = 0; i < nec; ++i) {
			microjxl__plane *cp = &f->gmodular.channel[3 + i];
			bgec[i] = 1.0f;
			if (cp->type != MICROJXL__PLANE_EMPTY && gx < cp->width && gy < cp->height) {
				int32_t p = cp->type == MICROJXL__PLANE_I32 ?
					MICROJXL__I32_PIXELS(cp, gy)[gx] : MICROJXL__I16_PIXELS(cp, gy)[gx];
				bgec[i] = (float) p / (float) maxpixel;
			}
		}
		/* extra channels first */
		for (i = 0; i < nec; ++i) {
			const microjxl__patch_blend *bl = &bls[1 + i];
			int m = bl->mode;
			int ac = bl->alpha_channel;
			int premult = im->ec_info[ac].data.alpha_associated;
			float bg = bgec[i];
			float fg = MICROJXL__F32_PIXELS(&rfp[3 + i], ry)[rx];
			float out;
			if (m == 0) out = bg;
			else if (m == 1) out = fg;
			else if (m == 2) out = bg + fg;
			else if (m == 3) {
				out = bg * (bl->clamp ? microjxl__patch_clamp01(fg) : fg);
			} else if (m == 4 || m == 5) {
				float fa, bga;
				if (m == 4) { /* fa = fg alpha, bga = bg alpha */
					fa = MICROJXL__F32_PIXELS(&rfp[3 + ac], ry)[rx];
					if (bl->clamp) fa = microjxl__patch_clamp01(fa);
					bga = bgec[ac];
				} else { /* fa = bg alpha (clamped), bga = fg alpha */
					fa = bgec[ac];
					if (bl->clamp) fa = microjxl__patch_clamp01(fa);
					bga = MICROJXL__F32_PIXELS(&rfp[3 + ac], ry)[rx];
				}
				out = microjxl__patch_blend_one(m, bg, fg, fa, bga, premult);
			} else if (m == 6 || m == 7) {
				float w;
				if (m == 6) {
					w = MICROJXL__F32_PIXELS(&rfp[3 + ac], ry)[rx];
				} else {
					w = bgec[ac];
				}
				if (bl->clamp) w = microjxl__patch_clamp01(w);
				out = microjxl__patch_blend_one(m, bg, fg, w, w, premult);
			} else out = bg;
			/* write back into the frame's EC plane */
			if (num_ec > i) {
				microjxl__plane *cp = &f->gmodular.channel[3 + i];
				if (cp->type != MICROJXL__PLANE_EMPTY && gx < cp->width && gy < cp->height) {
					int32_t q = (int32_t) (out * (float) maxpixel + 0.5f);
					if (cp->type == MICROJXL__PLANE_I32) MICROJXL__I32_PIXELS(cp, gy)[gx] = q;
					else MICROJXL__I16_PIXELS(cp, gy)[gx] = (int16_t) q;
				}
			}
		}
		/* colour channels (pre-blend alpha from the snapshot) */
		{
			int m = bls[0].mode;
			int ac = bls[0].alpha_channel;
			int premult = has_alpha ? im->ec_info[ac].data.alpha_associated : 0;
			for (i = 0; i < 3; ++i) {
				float bg = v[i], fg = fgc[i], out;
				if (m == 0) out = bg;
				else if (m == 1) out = fg;
				else if (m == 2) out = bg + fg;
				else if (m == 3) {
					out = bg * (bls[0].clamp ? microjxl__patch_clamp01(fg) : fg);
				} else if ((m == 4 || m == 5) && !has_alpha) {
					out = fg; /* no alpha: plain replace (matches the row version) */
				} else if (m == 4 || m == 5) {
					float fa, bga;
					if (m == 4) {
						fa = MICROJXL__F32_PIXELS(&rfp[3 + ac], ry)[rx];
						if (bls[0].clamp) fa = microjxl__patch_clamp01(fa);
						bga = bgec[ac];
					} else {
						fa = bgec[ac];
						if (bls[0].clamp) fa = microjxl__patch_clamp01(fa);
						bga = MICROJXL__F32_PIXELS(&rfp[3 + ac], ry)[rx];
					}
					out = microjxl__patch_blend_one(m, bg, fg, fa, bga, premult);
				} else if (m == 6 || m == 7) {
					if (!has_alpha) {
						out = bg + fg; /* no alpha: kAdd */
					} else {
						float w;
						if (m == 6) {
							w = MICROJXL__F32_PIXELS(&rfp[3 + ac], ry)[rx];
						} else {
							w = bgec[ac];
						}
						if (bls[0].clamp) w = microjxl__patch_clamp01(w);
						out = microjxl__patch_blend_one(m, bg, fg, w, w, premult);
					}
				} else out = bg;
				newc[i] = out;
			}
			/* colour kBlend also blends the alpha channel itself */
			if ((m == 4 || m == 5) && has_alpha) {
				float fa, bga, na;
				if (m == 4) {
					fa = MICROJXL__F32_PIXELS(&rfp[3 + alpha_c], ry)[rx];
					if (bls[0].clamp) fa = microjxl__patch_clamp01(fa);
					bga = bgec[alpha_c];
				} else {
					fa = bgec[alpha_c];
					if (bls[0].clamp) fa = microjxl__patch_clamp01(fa);
					bga = MICROJXL__F32_PIXELS(&rfp[3 + alpha_c], ry)[rx];
				}
				na = 1.0f - (1.0f - fa) * (1.0f - bga);
				if (num_ec > alpha_c) {
					microjxl__plane *cp = &f->gmodular.channel[3 + alpha_c];
					if (cp->type != MICROJXL__PLANE_EMPTY && gx < cp->width && gy < cp->height) {
						int32_t q = (int32_t) (na * (float) maxpixel + 0.5f);
						if (cp->type == MICROJXL__PLANE_I32) MICROJXL__I32_PIXELS(cp, gy)[gx] = q;
						else MICROJXL__I16_PIXELS(cp, gy)[gx] = (int16_t) q;
					}
				}
			}
		}
		v[0] = newc[0]; v[1] = newc[1]; v[2] = newc[2];
	}
	return 0;
MICROJXL__ON_ERROR:
	return st->err;
}

/* Build the frame's float patch/blend canvas: colour channels in the
 * frame's pre-colour-transform float domain plus num_ec float EC planes.
 * Colour domain: correlated XYB (X = ch1*m0, Y = ch0*m1, B = (ch2+ch0)*m2)
 * for xyb frames - the same domain the combine's samples and the
 * reference planes use, and where splines/noise apply; display-domain
 * samples divided by maxpixel otherwise. EC values are the render's
 * integer-domain samples divided by maxpixel. */
MICROJXL__STATIC_RETURNS_ERR microjxl__patch_canvas(microjxl__st *st, microjxl__plane *bgp) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t maxpixel = microjxl__maxpixel_scale(im);
	int32_t color_channels = (f->gmodular.num_channels >= 3) ? 3 : 1;
	int32_t num_ec = im->num_extra_channels;
	int32_t i, y, x;
	for (i = 0; i < 3 + num_ec; ++i) {
		MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &bgp[i]));
	}
	for (y = 0; y < f->height; ++y) {
		for (x = 0; x < f->width; ++x) {
			/* colour */
			if (im->xyb_encoded) {
				int32_t Y = MICROJXL__I32_PIXELS(&f->gmodular.channel[0], y)[x];
				int32_t X = MICROJXL__I32_PIXELS(&f->gmodular.channel[1], y)[x];
				int32_t BmY = MICROJXL__I32_PIXELS(&f->gmodular.channel[2], y)[x];
				MICROJXL__F32_PIXELS(&bgp[0], y)[x] = (float) X * f->m_lf_scaled[0];
				MICROJXL__F32_PIXELS(&bgp[1], y)[x] = (float) Y * f->m_lf_scaled[1];
				MICROJXL__F32_PIXELS(&bgp[2], y)[x] = (float) ((int64_t) BmY + Y) * f->m_lf_scaled[2];
			} else if (color_channels == 3) {
				for (i = 0; i < 3; ++i) {
					int32_t v = f->gmodular.channel[i].type == MICROJXL__PLANE_I32 ?
						MICROJXL__I32_PIXELS(&f->gmodular.channel[i], y)[x] :
						MICROJXL__I16_PIXELS(&f->gmodular.channel[i], y)[x];
					MICROJXL__F32_PIXELS(&bgp[i], y)[x] = (float) v / (float) maxpixel;
				}
			} else {
				int32_t v = f->gmodular.channel[0].type == MICROJXL__PLANE_I32 ?
					MICROJXL__I32_PIXELS(&f->gmodular.channel[0], y)[x] :
					MICROJXL__I16_PIXELS(&f->gmodular.channel[0], y)[x];
				for (i = 0; i < 3; ++i) MICROJXL__F32_PIXELS(&bgp[i], y)[x] = (float) v / (float) maxpixel;
			}
			/* extra channels (layout is colour channels first, then ECs —
			 * also for grayscale frames with a single colour channel) */
			for (i = 0; i < num_ec; ++i) {
				microjxl__plane *cp = &f->gmodular.channel[color_channels + i];
				int32_t v = cp->type == MICROJXL__PLANE_I32 ? MICROJXL__I32_PIXELS(cp, y)[x] :
					(cp->type == MICROJXL__PLANE_I16 ? MICROJXL__I16_PIXELS(cp, y)[x] : maxpixel);
				MICROJXL__F32_PIXELS(&bgp[3 + i], y)[x] = (float) v / (float) maxpixel;
			}
		}
	}
	return 0;
MICROJXL__ON_ERROR:
	for (i = 0; i < 3 + num_ec; ++i) microjxl__mem_free_plane(&bgp[i]);
	return st->err;
}	/* libjxl FinalizeFrame: a frame is saved to reference slot save_as_ref
	 * when it can be referenced (CanBeReferenced: not last, not LF, duration
	 * 0 or explicitly saved). The saved colour planes live in the correlated
	 * XYB float domain (X, Y, B — the combine's samples / splines / noise
	 * domain) for xyb frames, and display-domain/maxpixel samples otherwise. */
static int microjxl__frame_can_ref(const microjxl__frame_st *f) {
	return !f->is_last && f->type != MICROJXL__FRAME_LF &&
		(f->duration == 0 || f->save_as_ref != 0);
}

MICROJXL__STATIC_RETURNS_ERR microjxl__save_ref_frame(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t maxpixel = microjxl__maxpixel_scale(im);
	int32_t slot = f->save_as_ref;
	int32_t num_ec = im->num_extra_channels;
	int32_t nch = 3 + num_ec;
	int32_t i, y, x;
	microjxl__plane *planes = NULL;

	MICROJXL__SHOULD(slot >= 0 && slot < 4, "refs");
	if (im->ref_planes[slot]) {
		for (i = 0; i < im->ref_nch[slot]; ++i) microjxl__mem_free_plane(&im->ref_planes[slot][i]);
		microjxl__mem_free(im->ref_planes[slot]);
		im->ref_planes[slot] = NULL;
	}
	MICROJXL__TRY_CALLOC(microjxl__plane, &planes, (size_t) nch); // zeroed slots (free_plane on garbage would crash)
	im->ref_nch[slot] = nch;
	im->ref_w[slot] = f->upsampled_width;
	im->ref_h[slot] = f->upsampled_height;
	im->ref_in_xyb[slot] = f->save_before_ct;
	im->ref_present[slot] = 1;

	if (!f->is_modular) {
		/* VarDCT: combine filled ref_snap (3 raw correlated-XYB floats).
		 * EC planes come from gmodular (maxpixel-scaled ints). */
		for (i = 0; i < 3; ++i) {
			planes[i] = f->ref_snap[i];
			/* ownership moved into planes[i]: only zero the slot here.
			 * microjxl__mem_free_plane would release the pixels planes[i] now owns
			 * (double free when the slot is reused or at shutdown). */
			memset(&f->ref_snap[i], 0, sizeof(f->ref_snap[i]));
		}
		for (i = 3; i < nch; ++i) {
			microjxl__plane *cp = &f->gmodular.channel[i];
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &planes[i]));
			for (y = 0; y < f->height; ++y) {
				float *d = MICROJXL__F32_PIXELS(&planes[i], y);
				int32_t xx;
				if (cp->type == MICROJXL__PLANE_I32) {
					const int32_t *s = MICROJXL__I32_PIXELS(cp, y);
					for (xx = 0; xx < f->width; ++xx) d[xx] = (float) s[xx] / (float) maxpixel;
				} else if (cp->type == MICROJXL__PLANE_I16) {
					const int16_t *s = MICROJXL__I16_PIXELS(cp, y);
					for (xx = 0; xx < f->width; ++xx) d[xx] = (float) s[xx] / (float) maxpixel;
				} else {
					for (xx = 0; xx < f->width; ++xx) d[xx] = 1.0f;
				}
			}
		}
	} else {
		/* Modular: derive everything from gmodular. XYB colour via the
		 * render's recorrelation (correlated X/Y/B domain, matching the
		 * combine's samples and splines/noise); plain colour / EC via
		 * maxpixel scaling (libjxl ModularImageToDecodedRect). */
		int32_t color_channels = (f->gmodular.num_channels >= 3) ? 3 : 1;
		for (i = 0; i < 3; ++i) {
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &planes[i]));
		}
		for (y = 0; y < f->height; ++y) {
			for (x = 0; x < f->width; ++x) {
				if (im->xyb_encoded) {
					/* channels may be I16 (modular_16bit_buffers, the default) or
					 * I32; read through the type */
					int32_t chv[3], ci;
					for (ci = 0; ci < 3; ++ci) {
						microjxl__plane *cp = &f->gmodular.channel[ci];
						chv[ci] = cp->type == MICROJXL__PLANE_I32 ?
							MICROJXL__I32_PIXELS(cp, y)[x] :
							(cp->type == MICROJXL__PLANE_I16 ? MICROJXL__I16_PIXELS(cp, y)[x] : 0);
					}
					int32_t Y = chv[0], X = chv[1], BmY = chv[2];
					MICROJXL__F32_PIXELS(&planes[0], y)[x] = (float) X * f->m_lf_scaled[0];
					MICROJXL__F32_PIXELS(&planes[1], y)[x] = (float) Y * f->m_lf_scaled[1];
					MICROJXL__F32_PIXELS(&planes[2], y)[x] = (float) ((int64_t) BmY + Y) * f->m_lf_scaled[2];
				} else if (color_channels == 3) {
					for (i = 0; i < 3; ++i) {
						int32_t v = f->gmodular.channel[i].type == MICROJXL__PLANE_I32 ?
							MICROJXL__I32_PIXELS(&f->gmodular.channel[i], y)[x] :
							MICROJXL__I16_PIXELS(&f->gmodular.channel[i], y)[x];
						MICROJXL__F32_PIXELS(&planes[i], y)[x] = (float) v / (float) maxpixel;
					}
				} else {
					int32_t v = f->gmodular.channel[0].type == MICROJXL__PLANE_I32 ?
						MICROJXL__I32_PIXELS(&f->gmodular.channel[0], y)[x] :
						MICROJXL__I16_PIXELS(&f->gmodular.channel[0], y)[x];
					for (i = 0; i < 3; ++i) MICROJXL__F32_PIXELS(&planes[i], y)[x] = (float) v / (float) maxpixel;
				}
			}
		}
		for (i = 3; i < nch; ++i) {
			microjxl__plane *cp = &f->gmodular.channel[color_channels + (i - 3)];
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &planes[i]));
			for (y = 0; y < f->height; ++y) {
				float *d = MICROJXL__F32_PIXELS(&planes[i], y);
				int32_t xx;
				if (cp->type == MICROJXL__PLANE_I32) {
					const int32_t *s = MICROJXL__I32_PIXELS(cp, y);
					for (xx = 0; xx < f->width; ++xx) d[xx] = (float) s[xx] / (float) maxpixel;
				} else if (cp->type == MICROJXL__PLANE_I16) {
					const int16_t *s = MICROJXL__I16_PIXELS(cp, y);
					for (xx = 0; xx < f->width; ++xx) d[xx] = (float) s[xx] / (float) maxpixel;
				} else {
					for (xx = 0; xx < f->width; ++xx) d[xx] = 1.0f;
				}
			}
		}
	}
	im->ref_planes[slot] = planes;
	return 0;

MICROJXL__ON_ERROR:
	if (planes) {
		for (i = 0; i < nch; ++i) microjxl__mem_free_plane(&planes[i]);
		microjxl__mem_free(planes);
	}
	return st->err;
}

/* Store the post-blend display render (canvas-sized interleaved RGBA, [0,1]
 * display floats) of a visible referencable frame into reference slot
 * f->save_as_ref. libjxl stores the image bundle AFTER blending
 * (WriteToImageBundleStage runs post-Blending; FinalizeFrame moves it into
 * reference_frames[save_as_reference]), so the background a later blend
 * reads is the coalesced display render — exactly this plane. */
MICROJXL__STATIC_RETURNS_ERR microjxl__store_blend_ref(microjxl__st *st, const microjxl__plane *rgba) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t slot = f->save_as_ref;
	int32_t y;

	if (f->save_before_ct) return 0; // patch-domain ref only (ref_planes); no display-domain capture
	MICROJXL__SHOULD(slot >= 0 && slot < 4, "refs");
	MICROJXL__SHOULD(rgba->type == MICROJXL__PLANE_F32, "refs");
	microjxl__mem_free_plane(&im->ref_rgba[slot]);
	MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, im->width * 4, im->height, MICROJXL__PLANE_FORCE_PAD, &im->ref_rgba[slot]));
	{
		int32_t y;
		for (y = 0; y < im->height; ++y) {
			memcpy(MICROJXL__F32_PIXELS(&im->ref_rgba[slot], y), MICROJXL__F32_PIXELS(rgba, y), (size_t) im->width * 4 * sizeof(float));
		}
	}
	/* snapshot the current EC canvas for this slot (libjxl stores the whole
	 * image bundle, ECs included) so later frames' ProcessPaddingRow can
	 * copy padding areas from the EC's blend source slot */
	{
		int32_t e;
		for (e = 0; e < 4; ++e) {
			if (im->ref_ec[slot] && e < im->ref_ec_n[slot]) microjxl__mem_free_plane(&im->ref_ec[slot][e]);
		}
		if (im->ref_ec[slot] && im->ref_ec_n[slot] != im->num_ec_canvas) {
			microjxl__mem_free(im->ref_ec[slot]);
			im->ref_ec[slot] = NULL;
		}
		if (im->num_ec_canvas > 0) {
			if (!im->ref_ec[slot]) {
				im->ref_ec[slot] = (microjxl__plane *) microjxl__malloc((size_t) im->num_ec_canvas, sizeof(microjxl__plane));
				MICROJXL__SHOULD(im->ref_ec[slot], "!mem");
				for (e = 0; e < im->num_ec_canvas; ++e) microjxl__init_empty_plane(&im->ref_ec[slot][e]);
				im->ref_ec_n[slot] = im->num_ec_canvas;
			}
			for (e = 0; e < im->num_ec_canvas; ++e) {
				microjxl__plane *dst = &im->ref_ec[slot][e];
				const microjxl__plane *src = &im->ec_canvas[e];
				microjxl__mem_free_plane(dst);
				if (src->type == MICROJXL__PLANE_F32) {
					MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, src->width, src->height, MICROJXL__PLANE_FORCE_PAD, dst));
					for (y = 0; y < src->height; ++y)
						memcpy(MICROJXL__F32_PIXELS(dst, y), MICROJXL__F32_PIXELS(src, y), (size_t) src->width * sizeof(float));
				}
			}
		}
	}
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_plane(&im->ref_rgba[slot]);
	return st->err;
}

/* Apply the patch dictionary to the modular frame's gmodular planes
 * (in place), before the render. No-op for VarDCT frames (their apply
 * lives in the combine pixel loop). */
MICROJXL__STATIC_RETURNS_ERR microjxl__apply_patches_modular(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	microjxl__plane *bgp = NULL;
	int32_t num_ec = im->num_extra_channels;
	int32_t i, y;
	if (f->patches.num_pos == 0) return 0;
	MICROJXL__TRY_CALLOC(microjxl__plane, &bgp, (size_t) (3 + num_ec)); // zeroed slots
	MICROJXL__TRY(microjxl__patch_canvas(st, bgp));
	for (y = 0; y < f->height; ++y) {
		MICROJXL__TRY(microjxl__apply_patches_row(st, bgp, y));
	}
	/* write blended values back into gmodular (round to nearest int) */
	int32_t color_channels = (f->gmodular.num_channels >= 3) ? 3 : 1;
	for (y = 0; y < f->height; ++y) {
		for (i = 0; i < 3 + num_ec; ++i) {
			int32_t gi = (i < 3) ? ((color_channels == 3) ? i : 0)
			                     : (color_channels + (i - 3));
			microjxl__plane *cp = &f->gmodular.channel[gi];
			const float *src = MICROJXL__F32_PIXELS(&bgp[i], y);
			int32_t x;
			/* colour channels are only written back when the frame actually
			 * has 3 colour channels; the gray replicate case rewrites ch0 */
			if (i < 3 && color_channels == 1 && gi != 0) continue;
			for (x = 0; x < f->width; ++x) {
				float v = src[x] * (float) microjxl__maxpixel_scale(im);
				int32_t q = (int32_t) (v >= 0.0f ? v + 0.5f : v - 0.5f);
				if (cp->type == MICROJXL__PLANE_I32) MICROJXL__I32_PIXELS(cp, y)[x] = q;
				else MICROJXL__I16_PIXELS(cp, y)[x] = (int16_t) q;
			}
		}
	}
	for (i = 0; i < 3 + num_ec; ++i) microjxl__mem_free_plane(&bgp[i]);
	microjxl__mem_free(bgp);
	return 0;
MICROJXL__ON_ERROR:
	if (bgp) { for (i = 0; i < 3 + num_ec; ++i) microjxl__mem_free_plane(&bgp[i]); microjxl__mem_free(bgp); }
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_global(microjxl__st *st);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_global(microjxl__st *st) {
	microjxl__frame_st *f = st->frame;
	int32_t sidx = 0;
	int32_t i, j;

	if (f->has_patches) MICROJXL__TRY(microjxl__decode_patch_dict(st));
	if (f->has_splines) MICROJXL__TRY(microjxl__decode_splines(st));

	if (f->has_noise) {
		/* K.5.1: the noise LUT (eight entries) is read before the
		 * LfChannelDequantization bundle (see the G.1.1 bundle table), even
		 * for modular frames. Each entry is u(10) scaled by 1/1024. */
		for (i = 0; i < 8; ++i) f->noise_lut[i] = (float) microjxl__u(st, 10) / 1024.0f;
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_NOISE")) {
			fprintf(stderr, "[mj-noiselut]");
			for (i = 0; i < 8; ++i) fprintf(stderr, " %.7g", (double) f->noise_lut[i]);
			fprintf(stderr, "\n");
		}
#endif
	}

	if (!microjxl__u(st, 1)) { // LfChannelDequantization.all_default
		// TODO spec bug: missing division by 128
		for (i = 0; i < 3; ++i) {
			uint32_t w = (uint32_t) microjxl__u(st, 16);
			int32_t biased_exp = (int32_t) ((w >> 10) & 0x1f);
			float v = (w >> 15 ? -1.0f : 1.0f) * ldexpf((float) ((w & 0x3ff) | (biased_exp > 0 ? 0x400 : 0)), biased_exp - 25);
			f->m_lf_scaled[i] = v / 128.0f;
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_LFDQ")) fprintf(stderr, "[microjxl-lfdq] c=%d word=0x%04x val=%g\n", i, (unsigned) w, (double) f->m_lf_scaled[i]);
#endif
		}
	}

	if (!f->is_modular) {
		f->global_scale = microjxl__u32(st, 1, 11, 2049, 11, 4097, 12, 8193, 16);
		f->quant_lf = microjxl__u32(st, 16, 0, 1, 5, 1, 8, 1, 16);

		// HF block context
		if (microjxl__u(st, 1)) {
			static const uint8_t DEFAULT_BLKCTX[] = {
				0, 1, 2, 2, 3, 3, 4, 5, 6, 6, 6, 6, 6,
				7, 8, 9, 9, 10, 11, 12, 13, 14, 14, 14, 14, 14,
				7, 8, 9, 9, 10, 11, 12, 13, 14, 14, 14, 14, 14,
			};
			f->block_ctx_size = sizeof(DEFAULT_BLKCTX) / sizeof(*DEFAULT_BLKCTX);
			MICROJXL__TRY_MALLOC(uint8_t, &f->block_ctx_map, sizeof(DEFAULT_BLKCTX));
			memcpy(f->block_ctx_map, DEFAULT_BLKCTX, sizeof(DEFAULT_BLKCTX));
			f->nb_qf_thr = f->nb_lf_thr[0] = f->nb_lf_thr[1] = f->nb_lf_thr[2] = 0; // SPEC is implicit
			f->nb_block_ctx = 15;
		} else {
			MICROJXL__RAISE_DELAYED();
			f->block_ctx_size = 39; // SPEC not 27
			for (i = 0; i < 3; ++i) {
				f->nb_lf_thr[i] = microjxl__u(st, 4);
				// TODO spec question: should this be sorted? (current code is okay with that)
				for (j = 0; j < f->nb_lf_thr[i]; ++j) {
					f->lf_thr[i][j] = (int32_t) microjxl__unpack_signed64(microjxl__64u32(st, 0, 4, 16, 8, 272, 16, 65808, 32));
				}
				f->block_ctx_size *= f->nb_lf_thr[i] + 1; // SPEC is off by one
			}
			f->nb_qf_thr = microjxl__u(st, 4);
			// TODO spec bug: both qf_thr[i] and HfMul should be incremented
			for (i = 0; i < f->nb_qf_thr; ++i) f->qf_thr[i] = microjxl__u32(st, 0, 2, 4, 3, 12, 5, 44, 8) + 1;
			f->block_ctx_size *= f->nb_qf_thr + 1; // SPEC is off by one
			// block_ctx_size <= 39*15^4 and never overflows
			MICROJXL__SHOULD(f->block_ctx_size <= 39 * 64, "hfbc"); // SPEC limit is not 21*64
			MICROJXL__TRY(microjxl__cluster_map(st, f->block_ctx_size, 16, &f->nb_block_ctx, &f->block_ctx_map));
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_BCM")) {
				fprintf(stderr, "[mj-bcm] size=%d nb_lf=%d,%d,%d nb_qf=%d nbctx=%d map:",
					f->block_ctx_size, f->nb_lf_thr[0], f->nb_lf_thr[1], f->nb_lf_thr[2],
					f->nb_qf_thr, f->nb_block_ctx);
				for (i = 0; i < f->block_ctx_size; ++i) fprintf(stderr, " %d", f->block_ctx_map[i]);
				fprintf(stderr, "\n");
			}
#endif
		}

		if (!microjxl__u(st, 1)) { // LfChannelCorrelation.all_default
			f->inv_colour_factor = 1.0f / (float) microjxl__u32(st, 84, 0, 256, 0, 2, 8, 258, 16);
			f->base_corr_x = microjxl__f16(st);
			f->base_corr_b = microjxl__f16(st);
			f->x_factor_lf = microjxl__u(st, 8) - 128;
			f->b_factor_lf = microjxl__u(st, 8) - 128;
#ifdef MICROJXL_DEBUG
			fprintf(stderr, "[jcfl] color=%g base_x=%g base_b=%g xf=%d bf=%d\n",
				(double) (1.0f / f->inv_colour_factor), (double) f->base_corr_x,
				(double) f->base_corr_b, f->x_factor_lf, f->b_factor_lf);
#endif
		} else {
#ifdef MICROJXL_DEBUG
			fprintf(stderr, "[jcfl] all_default\n");
#endif
		}
	}

	/* K.4.2: the spline draw cache dequantizes with the base colour
	 * correlations, so it is built after LfChannelCorrelation is parsed
	 * (libjxl: InitializeDrawCache right after matrices.DecodeDC) */
	if (f->has_splines) MICROJXL__TRY(microjxl__spline_build_cache(st));

	// we need f->gmodular.num_channels for microjxl__tree
	MICROJXL__TRY(microjxl__init_modular_for_global(st, f->is_modular, f->do_ycbcr,
		f->log_upsampling, f->ec_log_upsampling, f->width, f->height, &f->gmodular));
#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[microjxl] lf_global: bitpos=%lld (after quant reads)\n", (long long) microjxl__bits_read(st));
#endif
	if (microjxl__u(st, 1)) { // global tree present
#ifdef MICROJXL_DEBUG
		fprintf(stderr, "[microjxl] decoding global tree (channels=%d) bitpos=%lld\n", f->gmodular.num_channels, (long long) microjxl__bits_read(st));
#endif
		int32_t max_tree_size = microjxl__min32(1 << 22,
			1024 + microjxl__clamp_mul32(microjxl__clamp_mul32(f->width, f->height), f->gmodular.num_channels) / 16);
		MICROJXL__TRY(microjxl__tree(st, max_tree_size, &f->global_tree, &f->global_codespec));
	}

	if (f->gmodular.num_channels > 0) {
		MICROJXL__TRY(microjxl__modular_header(st, f->global_tree, &f->global_codespec, &f->gmodular));
		MICROJXL__TRY(microjxl__allocate_modular(st, &f->gmodular));
		// SPEC H.3.6: the global modular stream decodes the meta channels plus
		// every following channel whose width and height fit within a group
		// (group_dim = 1 << group_size_shift). Decoding stops at the first
		// channel that does not fit; larger channels are decoded per group.
		// (libjxl's ModularDecode uses the same break semantics, and returns
		// without reading an ANS state when nothing decodes globally.)
		f->num_gm_channels = f->gmodular.nb_meta_channels;
		{
			int32_t gdim = 1 << f->group_size_shift;
			for (i = f->gmodular.nb_meta_channels; i < f->gmodular.num_channels; ++i) {
				microjxl__plane *gc = &f->gmodular.channel[i];
				if (gc->width > gdim || gc->height > gdim) break;
				f->num_gm_channels = i + 1;
			}
		}
	for (i = 0; i < f->num_gm_channels; ++i) {
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_STREAM")) fprintf(stderr, "[mjch] global-modular ch=%d sidx=%lld\n", i, (long long) sidx);
#endif
		MICROJXL__TRY(microjxl__modular_channel(st, &f->gmodular, i, sidx));
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_CHASH")) {
			microjxl__plane *pc = &f->gmodular.channel[i];
			uint64_t h = 1469598103934665603ull;
			int64_t s = 0; int32_t mn = INT32_MAX, mx = INT32_MIN, xx, yy; size_t n = 0;
			for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
				int32_t v; n++;
				if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx]; else v = MICROJXL__I16_PIXELS(pc, yy)[xx];
				h = (h ^ (uint64_t)(uint32_t)v) * 1099511628211ull;
				s += v; if (v < mn) mn = v; if (v > mx) mx = v;
			}
			fprintf(stderr, "[mjchash] sidx=%lld c=%d w=%d h=%d n=%zu min=%d max=%d sum=%lld hash=%016llx\n",
				(long long) sidx, i, pc->width, pc->height, n, mn, mx, (long long) s, (unsigned long long) h);
		}
#endif
	}
#ifdef MICROJXL_DEBUG
	for (i = 0; i < f->gmodular.num_channels; ++i) {
		microjxl__plane *pc = &f->gmodular.channel[i];
		if (pc->type != MICROJXL__PLANE_EMPTY && pc->width > 0 && pc->height > 0) {
			int64_t sum = 0, sumsq = 0; int32_t mn = INT32_MAX, mx = INT32_MIN, xx, yy, n = 0;
			for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
				int32_t v; n++;
				if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx]; else v = MICROJXL__I16_PIXELS(pc, yy)[xx];
				sum += v; sumsq += (int64_t) v * v;
				if (v < mn) mn = v; if (v > mx) mx = v;
			}
			fprintf(stderr, "[microjxl] ch%d %dx%d: n=%d min=%d max=%d mean=%.2f\n", i, pc->width, pc->height, n, mn, mx, n ? (double) sum / n : 0);
		}
	}
#endif
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_DUMP_PRE")) {
		FILE *pf = fopen("/tmp/microjxltest/preinv.bin", "wb");
		int32_t nc = f->gmodular.num_channels, nt = f->gmodular.nb_transforms, zz;
		fwrite(&nc, 4, 1, pf); fwrite(&nt, 4, 1, pf);
		fwrite(&f->gmodular.nb_meta_channels, 4, 1, pf);
		for (zz = 0; zz < nt; ++zz) {
			int32_t tr[6] = {0}; const microjxl__transform *t = &f->gmodular.transform[zz];
			tr[0] = t->tr;
			if (t->tr == MICROJXL__TR_SQUEEZE) { tr[1]=t->sq.horizontal; tr[2]=t->sq.in_place; tr[3]=t->sq.begin_c; tr[4]=t->sq.num_c; }
			fwrite(tr, 4, 6, pf);
		}
		for (zz = 0; zz < nc; ++zz) {
			microjxl__plane *pc = &f->gmodular.channel[zz];
			int32_t hdr[4] = {pc->width, pc->height, pc->hshift, pc->vshift};
			fwrite(hdr, 4, 4, pf);
			if (pc->type == MICROJXL__PLANE_I16) {
				int yy;
				for (yy = 0; yy < pc->height; ++yy) fwrite(MICROJXL__I16_PIXELS(pc, yy), 2, (size_t) pc->width, pf);
			} else if (pc->type == MICROJXL__PLANE_I32) {
				int yy;
				for (yy = 0; yy < pc->height; ++yy) fwrite(MICROJXL__I32_PIXELS(pc, yy), 4, (size_t) pc->width, pf);
			} else {
				int32_t zero = 0;
				fwrite(&zero, 4, 1, pf); // marker: empty
			}
		}
		fclose(pf);
	}
#endif
	if (f->num_gm_channels > 0) {
		MICROJXL__TRY(microjxl__finish_and_free_code(st, &f->gmodular.code));
	} else {
		// SPEC fully-modular frame with no meta channels: no channels are
		// decoded at the global level, so no initial ANS state is written
		// and none should be read/checked here (libjxl's ModularDecode
		// returns early when num_chans == 0). Just free the code object.
		microjxl__mem_free_code(&f->gmodular.code);
	}
	} else {
		f->num_gm_channels = 0;
	}

MICROJXL__ON_ERROR:
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// JPEG reconstruction (Part 2 §9.10, libjxl jpeg/jpeg_data.cc +
// dec_jpeg_data.cc + dec_jpeg_data_writer.cc). One driver decodes the jbrd
// box's JPEGData bitstream, fills the brotli-compressed payload chunks and
// re-serializes the JPEG. Enabled by frame_header.jpeg_recon (set when the
// container carried a jbrd box); coefficients are captured in dequant_hf
// and the JPEG is emitted when the final frame's last section completes.

typedef struct microjxl__brd_buffer {
	uint8_t *data;
	size_t size, cap;
	int failed;
} microjxl__brd_buffer;

MICROJXL_STATIC void microjxl__brd_put(microjxl__brd_buffer *b, const uint8_t *p, size_t n) {
	if (b->failed) return;
	if (b->size + n > b->cap) {
		size_t ncap = b->cap ? b->cap : 4096;
		uint8_t *nd;
		while (ncap < b->size + n) {
			size_t next = ncap + (ncap >> 1); /* grow 1.5x */
			if (next < ncap) { b->failed = 1; return; } /* size_t overflow */
			ncap = next;
		}
		nd = (uint8_t *) MICROJXL_MALLOC(ncap);
		if (!nd) { b->failed = 1; return; }
		if (b->data) { memcpy(nd, b->data, b->size); microjxl__mem_free(b->data); }
		b->data = nd; b->cap = ncap;
	}
	if (n) memcpy(b->data + b->size, p, n);
	b->size += n;
}

/* ---- JXL-style LSB-first bit reader over the jbrd payload (libjxl
 * Bundle::Read uses BitReader, which consumes 1..32 bits LSB-first) ---- */
typedef struct {
	const uint8_t *p;
	size_t size;
	size_t bitpos; // consumed bit count
	int overread;
} microjxl__brd_bits;

MICROJXL_STATIC uint32_t microjxl__brd_read(microjxl__brd_bits *b, int n) {
	uint32_t v = 0;
	int i;
	for (i = 0; i < n; ++i) {
		size_t byte = b->bitpos >> 3;
		int bit = (int) (b->bitpos & 7);
		if (byte >= b->size) { b->overread = 1; return v; }
		v |= (uint32_t) ((b->p[byte] >> bit) & 1) << i;
		b->bitpos++;
	}
	return v;
}

MICROJXL_STATIC uint32_t microjxl__brd_u32(
	microjxl__brd_bits *b,
	int choice0_bits, uint32_t choice0_off,
	int choice1_bits, uint32_t choice1_off,
	int choice2_bits, uint32_t choice2_off,
	int choice3_bits, uint32_t choice3_off
) {
	uint32_t sel = microjxl__brd_read(b, 2);
	int nbits = sel == 0 ? choice0_bits : sel == 1 ? choice1_bits : sel == 2 ? choice2_bits : choice3_bits;
	uint32_t off = sel == 0 ? choice0_off : sel == 1 ? choice1_off : sel == 2 ? choice2_off : choice3_off;
	uint32_t v = 0;
	if (nbits) v = microjxl__brd_read(b, (int) nbits);
	return off + v;
}

/* ---- JPEGData structs (libjxl jpeg_data.h) ---- */
#define MICROJXL__BRD_MAX_HUFF_TABLES 4
#define MICROJXL__BRD_MAX_NUM_PASSES 11
#define MICROJXL__BRD_ALPHABET_SIZE 256
#define MICROJXL__BRD_MAX_BIT_LENGTH 16
#define MICROJXL__BRD_DCT_BLOCK_SIZE 64
#define MICROJXL__BRD_CFL_PRECISION 11

typedef struct {
	int32_t id, h_samp_factor, v_samp_factor, quant_idx;
	int32_t width_in_blocks, height_in_blocks;
	int16_t *coeffs; // [width_in_blocks * height_in_blocks * 64], JPEG (natural) order
	size_t coeffs_cap;
} microjxl__brd_component;

typedef struct {
	int32_t precision, index, is_last;
	int32_t values[64]; // JPEG natural order
} microjxl__brd_quant;

typedef struct {
	int32_t is_ac, slot_id, is_last;
	int32_t counts[17];
	int32_t values[MICROJXL__BRD_ALPHABET_SIZE + 1];
	int32_t num_symbols;
} microjxl__brd_huffman;

typedef struct {
	int32_t comp_idx, ac_tbl_idx, dc_tbl_idx;
} microjxl__brd_comp_scan;

typedef struct {
	int32_t num_components;
	int32_t Ss, Se, Ah, Al, last_needed_pass;
	microjxl__brd_comp_scan comps[4];
	int32_t num_reset_points;
	int32_t *reset_points;
	int32_t num_extra_zero_runs;
	int32_t *ezr_block_idx, *ezr_count;
} microjxl__brd_scan;

typedef struct microjxl__brd_jpegdata {
	int is_gray;
	int32_t width, height;
	uint8_t marker_order[16384];
	size_t num_markers;
	int num_app_markers, num_com_markers, num_scans, num_intermarker, has_dri;
	/* per-APP: 0 = brotli'd (unknown), 1..4 = ICC, Exif, XMP, (5 = none-unused) */
	uint8_t *app_type; // [num_app_markers]
	uint8_t **app_data; size_t *app_size; // [num_app_markers]
	uint8_t **com_data; size_t *com_size; // [num_com_markers]
	uint8_t **inter_marker_data; size_t *inter_marker_size; // [num_intermarker]
	uint8_t *tail_data; size_t tail_size;
	int has_zero_padding_bit;
	uint8_t *padding_bits; size_t num_padding_bits;
	int32_t restart_interval;
	microjxl__brd_quant *quant; int32_t num_quant;
	microjxl__brd_component components[3]; int32_t num_components;
	microjxl__brd_huffman *huffman; int32_t num_huffman;
	microjxl__brd_scan *scans; int32_t num_scan_infos;
} microjxl__brd_jpegdata;

/* ---- the brotli payload chunks are read sequentially from the embedded
 * decoder: APPn (unknown only), COM, intermarker, tail ---- */
typedef struct {
	const uint8_t *in; size_t in_size, in_pos; // remaining brotli input
	uint8_t *out; size_t out_cap, out_pos; // decompressed output
} microjxl__brd_payload;

// reads exactly n bytes of decompressed output; returns 0 on success
MICROJXL_STATIC int microjxl__brd_payload_read(microjxl__brd_payload *pl, uint8_t *dst, size_t n) {
	if (n == 0) return 0; // zero-length payloads: avoid memcpy(dst==NULL, 0)
	if (n > pl->out_cap - pl->out_pos) return 1;
	memcpy(dst, pl->out + pl->out_pos, n);
	pl->out_pos += n;
	return 0;
}

/* ---- bit writer (libjxl dec_jpeg_data_writer.cc) ---- */
typedef struct {
	uint64_t put_buffer;
	int put_bits;
	microjxl__brd_buffer *out;
	int healthy;
} microjxl__brd_bwriter;

MICROJXL_STATIC void microjxl__brd_bw_init(microjxl__brd_bwriter *bw, microjxl__brd_buffer *out) {
	bw->put_buffer = 0;
	bw->put_bits = 64;
	bw->out = out;
	bw->healthy = 1;
}

MICROJXL_STATIC uint64_t microjxl__brd_has_zero_byte(uint64_t x) {
	return (x - 0x0101010101010101ULL) & ~x & 0x8080808080808080ULL;
}

/* emits up to 2 bytes: an extra 0x00 after every 0xFF */
MICROJXL_STATIC void microjxl__brd_emit_byte(microjxl__brd_bwriter *bw, int byte) {
	uint8_t b[2] = { (uint8_t) byte, 0 };
	microjxl__brd_put(bw->out, b, byte != 0xFF ? 1 : 2);
}

MICROJXL_STATIC void microjxl__brd_discharge(microjxl__brd_bwriter *bw, int nbits, uint64_t bits) {
	bw->put_buffer |= bits >> -bw->put_bits;
	if (microjxl__brd_has_zero_byte(~bw->put_buffer)) {
		int shift;
		for (shift = 56; shift >= 0; shift -= 8) microjxl__brd_emit_byte(bw, (int) ((bw->put_buffer >> shift) & 0xFF));
	} else {
		uint8_t be[8];
		int shift;
		for (shift = 56; shift >= 0; shift -= 8) be[(56 - shift) >> 3] = (uint8_t) ((bw->put_buffer >> shift) & 0xFF);
		microjxl__brd_put(bw->out, be, 8);
	}
	bw->put_bits += 64;
	bw->put_buffer = bits << bw->put_bits;
}

MICROJXL_STATIC void microjxl__brd_write_bits(microjxl__brd_bwriter *bw, int nbits, uint64_t bits) {
	bw->put_bits -= nbits;
	if (bw->put_bits < 0) {
		if (nbits > 64) { bw->put_bits += nbits; bw->healthy = 0; }
		else microjxl__brd_discharge(bw, nbits, bits);
	} else {
		bw->put_buffer |= bits << bw->put_bits;
	}
}

MICROJXL_STATIC void microjxl__brd_emit_marker(microjxl__brd_bwriter *bw, int marker) {
	uint8_t b[2] = { 0xFF, (uint8_t) marker };
	microjxl__brd_put(bw->out, b, 2);
}

/* pads with 1-bits (pad_bits == NULL, the reconstruction case: the stored
 * padding bits were not carried in the stream) */
MICROJXL_STATIC void microjxl__brd_jump_to_byte(microjxl__brd_bwriter *bw) {
	int n_bits = bw->put_bits & 7;
	uint8_t pad_pattern = (n_bits == 0) ? 0 : (uint8_t) ((1u << n_bits) - 1);
	while (bw->put_bits <= 56) {
		microjxl__brd_emit_byte(bw, (int) ((bw->put_buffer >> 56) & 0xFF));
		bw->put_buffer <<= 8;
		bw->put_bits += 8;
	}
	if (bw->put_bits < 64) {
		int pad_mask = 0xFFu >> (64 - bw->put_bits);
		int c = (int) (((bw->put_buffer >> 56) & ~(uint64_t) pad_mask) | pad_pattern);
		microjxl__brd_emit_byte(bw, c);
	}
	bw->put_buffer = 0;
	bw->put_bits = 64;
}

/* Huffman decoding table for re-encoding */
typedef struct {
	int32_t depth[MICROJXL__BRD_ALPHABET_SIZE];
	int32_t code[MICROJXL__BRD_ALPHABET_SIZE];
	int initialized;
} microjxl__brd_hufftable;

MICROJXL_STATIC int microjxl__brd_build_hufftable(const microjxl__brd_huffman *huff, microjxl__brd_hufftable *table) {
	int huff_code[MICROJXL__BRD_ALPHABET_SIZE];
	uint32_t huff_size[MICROJXL__BRD_ALPHABET_SIZE + 1];
	int p = 0, l, code, si, last_p;
	for (l = 1; l <= MICROJXL__BRD_MAX_BIT_LENGTH; ++l) {
		int i = huff->counts[l];
		if (p + i > MICROJXL__BRD_ALPHABET_SIZE + 1) return 0;
		while (i--) huff_size[p++] = (uint32_t) l;
	}
	if (p == 0) return 1;
	last_p = p - 1;
	huff_size[last_p] = 0; // reuse the sentinel element (the 256 EOI symbol)
	code = 0;
	si = huff_size[0];
	p = 0;
	while (huff_size[p]) {
		while (huff_size[p] == si) {
			huff_code[p++] = code;
			code++;
		}
		code <<= 1;
		si++;
	}
	for (p = 0; p < last_p; p++) {
		int i = huff->values[p];
		table->depth[i] = huff_size[p];
		table->code[i] = huff_code[p];
	}
	return 1;
}

/* progressive-scan entropy state (libjxl DCTCodingState) */
typedef struct {
	int32_t eob_run;
	microjxl__brd_hufftable *cur_ac_huff;
	uint16_t refinement_bits[64];
	size_t refinement_bits_count;
} microjxl__brd_coding_state;

MICROJXL_STATIC int microjxl__brd_floor_log2_nonzero(uint32_t v) {
	int r = 0;
	while (v >>= 1) r++;
	return r;
}

MICROJXL_STATIC void microjxl__brd_flush(microjxl__brd_coding_state *s, microjxl__brd_bwriter *bw) {
	if (s->eob_run > 0) {
		int nbits = microjxl__brd_floor_log2_nonzero((uint32_t) s->eob_run);
		int symbol = nbits << 4;
		microjxl__brd_write_bits(bw, s->cur_ac_huff->depth[symbol], (uint64_t) s->cur_ac_huff->code[symbol]);
		if (nbits > 0) microjxl__brd_write_bits(bw, nbits, (uint64_t) (s->eob_run & ((1 << nbits) - 1)));
		s->eob_run = 0;
	}
	{
		const size_t kStride = 124; // (515 - 16) / 2 / 2
		size_t num_words = s->refinement_bits_count >> 4;
		size_t i = 0;
		while (i < num_words) {
			size_t limit = i + kStride < num_words ? i + kStride : num_words;
			for (; i < limit; ++i) microjxl__brd_write_bits(bw, 16, s->refinement_bits[i]);
		}
	}
	{
		size_t tail = s->refinement_bits_count & 0xF;
		if (tail) microjxl__brd_write_bits(bw, (int) tail, s->refinement_bits[(s->refinement_bits_count - 1) >> 4]);
	}
	s->refinement_bits_count = 0;
}

MICROJXL_STATIC void microjxl__brd_buffer_eob(microjxl__brd_coding_state *s, microjxl__brd_hufftable *ac_huff,
	const int *new_bits_array, size_t new_bits_count, microjxl__brd_bwriter *bw) {
	if (s->eob_run == 0) s->cur_ac_huff = ac_huff;
	++s->eob_run;
	if (new_bits_count) {
		uint64_t new_bits = 0;
		size_t i;
		for (i = 0; i < new_bits_count; ++i) new_bits = (new_bits << 1) | (uint64_t) new_bits_array[i];
		{
			size_t tail = s->refinement_bits_count & 0xF;
			if (tail) { // first stuff the tail item
				size_t stuff_bits_count = 16 - tail < new_bits_count ? 16 - tail : new_bits_count;
				uint16_t stuff_bits = (uint16_t) (new_bits >> (new_bits_count - stuff_bits_count));
				stuff_bits &= (uint16_t) (((1u << stuff_bits_count) - 1));
				s->refinement_bits[(s->refinement_bits_count - 1) >> 4] =
					(uint16_t) ((s->refinement_bits[(s->refinement_bits_count - 1) >> 4] << stuff_bits_count) | stuff_bits);
				new_bits_count -= stuff_bits_count;
				s->refinement_bits_count += stuff_bits_count;
			}
		}
		while (new_bits_count >= 16) {
			s->refinement_bits[s->refinement_bits_count >> 4] = (uint16_t) (new_bits >> (new_bits_count - 16));
			new_bits_count -= 16;
			s->refinement_bits_count += 16;
		}
		if (new_bits_count) {
			s->refinement_bits[s->refinement_bits_count >> 4] = (uint16_t) (new_bits & ((1u << new_bits_count) - 1));
			s->refinement_bits_count += new_bits_count;
		}
	}
	if (s->eob_run == 0x7FFF) microjxl__brd_flush(s, bw);
}

MICROJXL_STATIC const uint32_t MICROJXL__BRD_NATURAL_ORDER[80] = {
	  0,   1,  8, 16,  9,  2,  3, 10,
	 17, 24, 32, 25, 18, 11,  4,  5,
	 12, 19, 26, 33, 40, 48, 41, 34,
	 27, 20, 13,  6,  7, 14, 21, 28,
	 35, 42, 49, 56, 57, 50, 43, 36,
	 29, 22, 15, 23, 30, 37, 44, 51,
	 58, 59, 52, 45, 38, 31, 39, 46,
	 53, 60, 61, 54, 47, 55, 62, 63,
	 63, 63, 63, 63, 63, 63, 63, 63,
	 63, 63, 63, 63, 63, 63, 63, 63
};

MICROJXL_STATIC int microjxl__brd_encode_dct_block_sequential(
	const int16_t *coeffs, microjxl__brd_hufftable *dc_huff, microjxl__brd_hufftable *ac_huff,
	int num_zero_runs, int32_t *last_dc_coeff, microjxl__brd_bwriter *bw) {
	int32_t temp2, temp, litmus = 0;
	int dc_nbits, i;
	int16_t r = 0;
	temp2 = coeffs[0];
	temp = temp2 - *last_dc_coeff;
	*last_dc_coeff = temp2;
	temp2 = temp >> (8 * sizeof(int32_t) - 1);
	temp += temp2;
	temp2 ^= temp;
	dc_nbits = (temp2 == 0) ? 0 : (microjxl__brd_floor_log2_nonzero((uint32_t) temp2) + 1);
	microjxl__brd_write_bits(bw, dc_huff->depth[dc_nbits], (uint64_t) dc_huff->code[dc_nbits]);
	if (dc_nbits) microjxl__brd_write_bits(bw, dc_nbits, (uint64_t) (temp & ((1u << dc_nbits) - 1)));
	for (i = 1; i < 64; ++i) {
		temp = coeffs[MICROJXL__BRD_NATURAL_ORDER[i]];
		if (temp == 0) { r++; continue; }
		temp2 = temp >> (8 * sizeof(int32_t) - 1);
		temp += temp2;
		temp2 ^= temp;
		if (r > 15) {
			for (; r > 15; r -= 16) microjxl__brd_write_bits(bw, ac_huff->depth[0xf0], (uint64_t) ac_huff->code[0xf0]);
		}
		litmus |= temp2;
		{
			int ac_nbits = microjxl__brd_floor_log2_nonzero((uint32_t) (uint16_t) temp2) + 1;
			int symbol = (r << 4) + ac_nbits;
			microjxl__brd_write_bits(bw, ac_huff->depth[symbol] + ac_nbits,
				((uint64_t) ac_huff->code[symbol] << ac_nbits) | ((uint64_t) (temp & ((1 << ac_nbits) - 1))));
		}
		r = 0;
	}
	for (i = 0; i < num_zero_runs; ++i) microjxl__brd_write_bits(bw, ac_huff->depth[0xf0], (uint64_t) ac_huff->code[0xf0]);
	if (r > 0) microjxl__brd_write_bits(bw, ac_huff->depth[0], (uint64_t) ac_huff->code[0]);
	return litmus >= 0;
}

MICROJXL_STATIC int microjxl__brd_encode_dct_block_progressive(
	const int16_t *coeffs, microjxl__brd_hufftable *dc_huff, microjxl__brd_hufftable *ac_huff,
	int Ss, int Se, int Al, int num_zero_runs,
	microjxl__brd_coding_state *coding_state, int32_t *last_dc_coeff, microjxl__brd_bwriter *bw) {
	int eob_run_allowed = Ss > 0;
	int32_t temp2, temp;
	int r, k;
	if (Ss == 0) {
		temp2 = coeffs[0] >> Al;
		temp = temp2 - *last_dc_coeff;
		*last_dc_coeff = temp2;
		temp2 = temp;
		if (temp < 0) { temp = -temp; if (temp < 0) return 0; temp2--; }
		{
			int nbits = (temp == 0) ? 0 : (microjxl__brd_floor_log2_nonzero((uint32_t) temp) + 1);
			microjxl__brd_write_bits(bw, dc_huff->depth[nbits], (uint64_t) dc_huff->code[nbits]);
			if (nbits) microjxl__brd_write_bits(bw, nbits, (uint64_t) (temp2 & ((1 << nbits) - 1)));
		}
		++Ss;
	}
	if (Ss > Se) return 1;
	r = 0;
	for (k = Ss; k <= Se; ++k) {
		temp = coeffs[MICROJXL__BRD_NATURAL_ORDER[k]];
		if (temp == 0) { r++; continue; }
		if (temp < 0) { temp = -temp; if (temp < 0) return 0; temp >>= Al; temp2 = ~temp; }
		else { temp >>= Al; temp2 = temp; }
		if (temp == 0) { r++; continue; }
		microjxl__brd_flush(coding_state, bw);
		while (r > 15) {
			microjxl__brd_write_bits(bw, ac_huff->depth[0xf0], (uint64_t) ac_huff->code[0xf0]);
			r -= 16;
		}
		{
			int nbits = microjxl__brd_floor_log2_nonzero((uint32_t) temp) + 1;
			int symbol = (r << 4) + nbits;
			microjxl__brd_write_bits(bw, ac_huff->depth[symbol], (uint64_t) ac_huff->code[symbol]);
			microjxl__brd_write_bits(bw, nbits, (uint64_t) (temp2 & ((1 << nbits) - 1)));
		}
		r = 0;
	}
	if (num_zero_runs > 0) {
		int i;
		microjxl__brd_flush(coding_state, bw);
		for (i = 0; i < num_zero_runs; ++i) {
			microjxl__brd_write_bits(bw, ac_huff->depth[0xf0], (uint64_t) ac_huff->code[0xf0]);
			r -= 16;
		}
	}
	if (r > 0) {
		microjxl__brd_buffer_eob(coding_state, ac_huff, NULL, 0, bw);
		if (!eob_run_allowed) microjxl__brd_flush(coding_state, bw);
	}
	return 1;
}

MICROJXL_STATIC int microjxl__brd_encode_refinement_bits(
	const int16_t *coeffs, microjxl__brd_hufftable *ac_huff,
	int Ss, int Se, int Al, microjxl__brd_coding_state *coding_state, microjxl__brd_bwriter *bw) {
	int eob_run_allowed = Ss > 0;
	int abs_values[64];
	int eob = 0, r = 0, k;
	int refinement_bits[64];
	size_t refinement_bits_count = 0;
	if (Ss == 0) {
		microjxl__brd_write_bits(bw, 1, (uint64_t) ((coeffs[0] >> Al) & 1));
		++Ss;
	}
	if (Ss > Se) return 1;
	for (k = Ss; k <= Se; k++) {
		int32_t abs_val = coeffs[MICROJXL__BRD_NATURAL_ORDER[k]];
		if (abs_val < 0) abs_val = -abs_val;
		abs_values[k] = abs_val >> Al;
		if (abs_values[k] == 1) eob = k;
	}
	for (k = Ss; k <= Se; k++) {
		if (abs_values[k] == 0) { r++; continue; }
		while (r > 15 && k <= eob) {
			size_t i;
			microjxl__brd_flush(coding_state, bw);
			microjxl__brd_write_bits(bw, ac_huff->depth[0xf0], (uint64_t) ac_huff->code[0xf0]);
			r -= 16;
			for (i = 0; i < refinement_bits_count; ++i) microjxl__brd_write_bits(bw, 1, refinement_bits[i]);
			refinement_bits_count = 0;
		}
		if (abs_values[k] > 1) {
			refinement_bits[refinement_bits_count++] = abs_values[k] & 1u;
			continue;
		}
		{
			size_t i;
			int symbol = (r << 4) + 1;
			int new_non_zero_bit = (coeffs[MICROJXL__BRD_NATURAL_ORDER[k]] < 0) ? 0 : 1;
			microjxl__brd_flush(coding_state, bw);
			microjxl__brd_write_bits(bw, ac_huff->depth[symbol], (uint64_t) ac_huff->code[symbol]);
			microjxl__brd_write_bits(bw, 1, (uint64_t) new_non_zero_bit);
			for (i = 0; i < refinement_bits_count; ++i) microjxl__brd_write_bits(bw, 1, refinement_bits[i]);
			refinement_bits_count = 0;
		}
		r = 0;
	}
	if (r > 0 || refinement_bits_count) {
		microjxl__brd_buffer_eob(coding_state, ac_huff, refinement_bits, refinement_bits_count, bw);
		if (!eob_run_allowed) microjxl__brd_flush(coding_state, bw);
	}
	return 1;
}

MICROJXL_STATIC void microjxl__brd_calculate_mcu_size(
	const microjxl__brd_jpegdata *jpg, const microjxl__brd_scan *scan, int *mcus_per_row, int *mcu_rows) {
	int is_interleaved = (scan->num_components > 1);
	const microjxl__brd_component *base = &jpg->components[scan->comps[0].comp_idx];
	int h_group = is_interleaved ? 1 : base->h_samp_factor;
	int v_group = is_interleaved ? 1 : base->v_samp_factor;
	int max_h = 1, max_v = 1, i;
	for (i = 0; i < jpg->num_components; ++i) {
		if (jpg->components[i].h_samp_factor > max_h) max_h = jpg->components[i].h_samp_factor;
		if (jpg->components[i].v_samp_factor > max_v) max_v = jpg->components[i].v_samp_factor;
	}
	*mcus_per_row = (jpg->width * h_group + 8 * max_h - 1) / (8 * max_h);
	*mcu_rows = (jpg->height * v_group + 8 * max_v - 1) / (8 * max_v);
}

MICROJXL_STATIC void microjxl__brd_free_jpegdata(microjxl__brd_jpegdata *jpg) {
	int32_t i;
	/* the marker-count arrays may not exist yet when parse fails early
	 * (counts are final before their arrays are allocated), so guard the
	 * per-element frees on the array pointer — the counts alone would
	 * index a NULL/garbage array */
	if (jpg->app_data) {
		for (i = 0; i < jpg->num_app_markers; ++i) microjxl__mem_free(jpg->app_data[i]);
	}
	microjxl__mem_free(jpg->app_data); microjxl__mem_free(jpg->app_size); microjxl__mem_free(jpg->app_type);
	if (jpg->com_data) {
		for (i = 0; i < jpg->num_com_markers; ++i) microjxl__mem_free(jpg->com_data[i]);
	}
	microjxl__mem_free(jpg->com_data); microjxl__mem_free(jpg->com_size);
	if (jpg->inter_marker_data) {
		for (i = 0; i < jpg->num_intermarker; ++i) microjxl__mem_free(jpg->inter_marker_data[i]);
	}
	microjxl__mem_free(jpg->inter_marker_data); microjxl__mem_free(jpg->inter_marker_size);
	microjxl__mem_free(jpg->tail_data);
	microjxl__mem_free(jpg->padding_bits);
	microjxl__mem_free(jpg->quant);
	for (i = 0; i < 3; ++i) microjxl__mem_free(jpg->components[i].coeffs);
	microjxl__mem_free(jpg->huffman);
	if (jpg->scans) {
		for (i = 0; i < jpg->num_scan_infos; ++i) {
			microjxl__mem_free(jpg->scans[i].reset_points);
			microjxl__mem_free(jpg->scans[i].ezr_block_idx);
			microjxl__mem_free(jpg->scans[i].ezr_count);
		}
		microjxl__mem_free(jpg->scans);
	}
	memset(jpg, 0, sizeof(*jpg));
}

/* ---- JPEGData::VisitFields port (read side) ----
 * `enc` points at the codestream-entropy-coded jbrd content (after the U64
 * output-size varint, which is also its first re-encoded byte). Returns a
 * jpegdata on success (caller frees with microjxl__brd_free_jpegdata). */
MICROJXL_STATIC int microjxl__brd_parse(
	microjxl__st *st, const uint8_t *enc, size_t enc_size, microjxl__brd_jpegdata *jpg, size_t *bitpos_out
) {
	microjxl__brd_bits b;
	int i;
	uint32_t marker;
	memset(jpg, 0, sizeof(*jpg));
	b.p = enc; b.size = enc_size; b.bitpos = 0; b.overread = 0;

	jpg->is_gray = (int) microjxl__brd_read(&b, 1);
	jpg->num_components = jpg->is_gray ? 1 : 3;

	marker = 0xc0;
	do {
		uint32_t marker32 = marker - 0xc0;
		marker32 = microjxl__brd_read(&b, 6);
		marker = marker32 + 0xc0;
		if (jpg->num_markers >= 16384) MICROJXL__RAISE("jbrd");
		jpg->marker_order[jpg->num_markers++] = (uint8_t) marker;
		if ((marker & 0xf0) == 0xe0) jpg->num_app_markers++;
		if (marker == 0xfe) jpg->num_com_markers++;
		if (marker == 0xda) jpg->num_scans++;
		if (marker == 0xff) jpg->num_intermarker++;
		if (marker == 0xdd) jpg->has_dri = 1;
		if (b.overread) MICROJXL__RAISE("jbrd");
	} while (marker != 0xd9);
	if (jpg->num_scans == 0) MICROJXL__RAISE("jbrd");

	/* APP markers */
	if (jpg->num_app_markers) {
		MICROJXL__TRY_MALLOC(uint8_t, &jpg->app_type, (size_t) jpg->num_app_markers);
		MICROJXL__TRY_MALLOC(uint8_t *, &jpg->app_data, (size_t) jpg->num_app_markers);
		MICROJXL__TRY_MALLOC(size_t, &jpg->app_size, (size_t) jpg->num_app_markers);
		/* zero the pointer array so a mid-fill error path frees only
		 * initialized elements (free_jpegdata walks it by count) */
		memset(jpg->app_data, 0, sizeof(uint8_t *) * (size_t) jpg->num_app_markers);
	}
	for (i = 0; i < jpg->num_app_markers; ++i) {
		uint32_t type = microjxl__brd_u32(&b, 0,0, 0,1, 1,2, 2,4);
		uint32_t len;
		if (type > 3) MICROJXL__RAISE("jbrd");
		jpg->app_type[i] = (uint8_t) type;
		len = microjxl__brd_read(&b, 16);
		if (len + 1 < 3) MICROJXL__RAISE("jbrd");
		jpg->app_size[i] = len + 1;
		MICROJXL__TRY_MALLOC(uint8_t, &jpg->app_data[i], jpg->app_size[i]);
		memset(jpg->app_data[i], 0, jpg->app_size[i]);
		if (b.overread) MICROJXL__RAISE("jbrd");
	}
	/* COM markers */
	if (jpg->num_com_markers) {
		MICROJXL__TRY_MALLOC(uint8_t *, &jpg->com_data, (size_t) jpg->num_com_markers);
		MICROJXL__TRY_MALLOC(size_t, &jpg->com_size, (size_t) jpg->num_com_markers);
		memset(jpg->com_data, 0, sizeof(uint8_t *) * (size_t) jpg->num_com_markers);
	}
	for (i = 0; i < jpg->num_com_markers; ++i) {
		uint32_t len = microjxl__brd_read(&b, 16);
		if (len + 1 < 3) MICROJXL__RAISE("jbrd");
		jpg->com_size[i] = len + 1;
		MICROJXL__TRY_MALLOC(uint8_t, &jpg->com_data[i], jpg->com_size[i]);
		memset(jpg->com_data[i], 0, jpg->com_size[i]);
		if (b.overread) MICROJXL__RAISE("jbrd");
	}

	/* quant tables */
	{
		uint32_t num_quant_tables = microjxl__brd_u32(&b, 0,1, 0,2, 0,3, 0,4);
		if (num_quant_tables == 4) MICROJXL__RAISE("jbrd");
		jpg->num_quant = (int32_t) num_quant_tables;
		MICROJXL__TRY_MALLOC(microjxl__brd_quant, &jpg->quant, (size_t) num_quant_tables);
		memset(jpg->quant, 0, sizeof(microjxl__brd_quant) * num_quant_tables);
		for (i = 0; i < (int32_t) num_quant_tables; ++i) {
			jpg->quant[i].precision = (int32_t) microjxl__brd_read(&b, 1);
			jpg->quant[i].index = (int32_t) microjxl__brd_read(&b, 2);
			jpg->quant[i].is_last = (int32_t) microjxl__brd_read(&b, 1);
		}
		if (b.overread) MICROJXL__RAISE("jbrd");
	}

	/* components */
	{
		uint32_t component_type = microjxl__brd_read(&b, 2);
		if (component_type == 0) {
			jpg->num_components = 1;
			jpg->components[0].id = 1;
		} else if (component_type == 1) {
			jpg->num_components = 3;
			jpg->components[0].id = 1; jpg->components[1].id = 2; jpg->components[2].id = 3;
		} else if (component_type == 2) {
			jpg->num_components = 3;
			jpg->components[0].id = 'R'; jpg->components[1].id = 'G'; jpg->components[2].id = 'B';
		} else {
			uint32_t num_components = microjxl__brd_u32(&b, 0,1, 0,2, 0,3, 0,4);
			if (num_components != 1 && num_components != 3) MICROJXL__RAISE("jbrd");
			jpg->num_components = (int32_t) num_components;
			for (i = 0; i < jpg->num_components; ++i) jpg->components[i].id = (int32_t) microjxl__brd_read(&b, 8);
		}
		for (i = 0; i < jpg->num_components; ++i) {
			jpg->components[i].quant_idx = (int32_t) microjxl__brd_read(&b, 2);
			if (jpg->components[i].quant_idx >= jpg->num_quant) MICROJXL__RAISE("jbrd");
		}
		if (b.overread) MICROJXL__RAISE("jbrd");
	}

	/* huffman codes */
	{
		uint32_t num_huff = microjxl__brd_u32(&b, 0,4, 3,2, 4,10, 6,26);
		jpg->num_huffman = (int32_t) num_huff;
		MICROJXL__TRY_MALLOC(microjxl__brd_huffman, &jpg->huffman, (size_t) num_huff);
		memset(jpg->huffman, 0, sizeof(microjxl__brd_huffman) * num_huff);
		for (i = 0; i < jpg->num_huffman; ++i) {
			microjxl__brd_huffman *hc = &jpg->huffman[i];
			int32_t is_ac = (int32_t) microjxl__brd_read(&b, 1);
			int32_t id = (int32_t) microjxl__brd_read(&b, 2);
			int32_t num_symbols = 0, j;
			hc->is_ac = is_ac;
			hc->slot_id = (is_ac << 4) | id;
			hc->is_last = (int32_t) microjxl__brd_read(&b, 1);
			for (j = 0; j <= 16; ++j) {
				hc->counts[j] = (int32_t) microjxl__brd_u32(&b, 0,0, 0,1, 3,2, 8,0);
				num_symbols += hc->counts[j];
			}
			if (num_symbols == 0) continue; // empty DHT marker
			if (num_symbols > MICROJXL__BRD_ALPHABET_SIZE + 1) MICROJXL__RAISE("jbrd");
			hc->num_symbols = num_symbols;
			{
				uint64_t value_slots[5] = {0};
				int dup = 0;
				for (j = 0; j < num_symbols; ++j) {
					uint32_t v = microjxl__brd_u32(&b, 2,0, 2,4, 4,8, 8,1);
					if (v > 256) MICROJXL__RAISE("jbrd");
					hc->values[j] = (int32_t) v;
					if ((value_slots[v >> 6] >> (v & 0x3F)) & 1) dup = 1;
					value_slots[v >> 6] |= (uint64_t) 1 << (v & 0x3F);
				}
				if (hc->values[num_symbols - 1] != MICROJXL__BRD_ALPHABET_SIZE) MICROJXL__RAISE("jbrd");
				if (value_slots[4] != 1) MICROJXL__RAISE("jbrd");
				if (dup) MICROJXL__RAISE("jbrd");
				if (!is_ac) {
					int only_dc = ((value_slots[0] >> 12) | value_slots[1] | value_slots[2] | value_slots[3]) == 0;
					if (!only_dc) MICROJXL__RAISE("jbrd");
				}
			}
			if (b.overread) MICROJXL__RAISE("jbrd");
		}
	}

	/* scans */
	jpg->num_scan_infos = jpg->num_scans;
	MICROJXL__TRY_MALLOC(microjxl__brd_scan, &jpg->scans, (size_t) jpg->num_scans);
	memset(jpg->scans, 0, sizeof(microjxl__brd_scan) * (size_t) jpg->num_scans);
	for (i = 0; i < jpg->num_scans; ++i) {
		microjxl__brd_scan *scan = &jpg->scans[i];
		int32_t j;
		scan->num_components = (int32_t) microjxl__brd_u32(&b, 0,1, 0,2, 0,3, 0,4);
		if (scan->num_components >= 4) MICROJXL__RAISE("jbrd");
		scan->Ss = (int32_t) microjxl__brd_read(&b, 6);
		scan->Se = (int32_t) microjxl__brd_read(&b, 6);
		scan->Al = (int32_t) microjxl__brd_read(&b, 4);
		scan->Ah = (int32_t) microjxl__brd_read(&b, 4);
		for (j = 0; j < scan->num_components; ++j) {
			scan->comps[j].comp_idx = (int32_t) microjxl__brd_read(&b, 2);
			if (scan->comps[j].comp_idx >= jpg->num_components) MICROJXL__RAISE("jbrd");
			scan->comps[j].ac_tbl_idx = (int32_t) microjxl__brd_read(&b, 2);
			scan->comps[j].dc_tbl_idx = (int32_t) microjxl__brd_read(&b, 2);
		}
		scan->last_needed_pass = (int32_t) microjxl__brd_u32(&b, 0,0, 0,1, 0,2, 3,3);
		if (b.overread) MICROJXL__RAISE("jbrd");
	}

	/* restart interval */
	if (jpg->has_dri) jpg->restart_interval = (int32_t) microjxl__brd_read(&b, 16);

	/* per-scan reset points and extra zero runs (delta-encoded block indices) */
	for (i = 0; i < jpg->num_scans; ++i) {
		microjxl__brd_scan *scan = &jpg->scans[i];
		int32_t j, last_block_idx;
		uint32_t num_reset_points = microjxl__brd_u32(&b, 0,0, 2,1, 4,4, 16,20);
		scan->num_reset_points = (int32_t) num_reset_points;
		if (num_reset_points) {
			MICROJXL__TRY_MALLOC(int32_t, &scan->reset_points, num_reset_points);
		}
		last_block_idx = -1;
		for (j = 0; j < (int32_t) num_reset_points; ++j) {
			uint32_t block_idx = microjxl__brd_u32(&b, 0,0, 3,1, 5,9, 28,41);
			block_idx += (uint32_t) last_block_idx + 1;
			if (block_idx >= (3u << 26)) MICROJXL__RAISE("jbrd");
			scan->reset_points[j] = (int32_t) block_idx;
			last_block_idx = (int32_t) block_idx;
		}
		{
			uint32_t num_extra_zero_runs = microjxl__brd_u32(&b, 0,0, 2,1, 4,4, 16,20);
			scan->num_extra_zero_runs = (int32_t) num_extra_zero_runs;
			if (num_extra_zero_runs) {
				MICROJXL__TRY_MALLOC(int32_t, &scan->ezr_block_idx, num_extra_zero_runs);
				MICROJXL__TRY_MALLOC(int32_t, &scan->ezr_count, num_extra_zero_runs);
			}
			last_block_idx = -1;
			for (j = 0; j < (int32_t) num_extra_zero_runs; ++j) {
				uint32_t count = microjxl__brd_u32(&b, 0,1, 2,2, 4,5, 8,20);
				uint32_t block_idx = microjxl__brd_u32(&b, 0,0, 3,1, 5,9, 28,41);
				block_idx += (uint32_t) last_block_idx + 1;
				if (count > 4 || block_idx > (3u << 26)) MICROJXL__RAISE("jbrd");
				scan->ezr_block_idx[j] = (int32_t) block_idx;
				scan->ezr_count[j] = (int32_t) count;
				last_block_idx = (int32_t) block_idx;
			}
		}
		if (b.overread) MICROJXL__RAISE("jbrd");
	}

	/* intermarker data sizes + tail data size */
	if (jpg->num_intermarker) {
		MICROJXL__TRY_MALLOC(uint8_t *, &jpg->inter_marker_data, (size_t) jpg->num_intermarker);
		MICROJXL__TRY_MALLOC(size_t, &jpg->inter_marker_size, (size_t) jpg->num_intermarker);
		memset(jpg->inter_marker_data, 0, sizeof(uint8_t *) * (size_t) jpg->num_intermarker);
	}
	for (i = 0; i < jpg->num_intermarker; ++i) {
		uint32_t len = microjxl__brd_read(&b, 16);
		jpg->inter_marker_size[i] = len;
		if (len) {
			MICROJXL__TRY_MALLOC(uint8_t, &jpg->inter_marker_data[i], len);
			memset(jpg->inter_marker_data[i], 0, len);
		}
	}
	{
		uint32_t tail_data_len = microjxl__brd_u32(&b, 0,0, 8,1, 16,257, 22,65793);
		jpg->tail_size = tail_data_len;
		if (tail_data_len) {
			MICROJXL__TRY_MALLOC(uint8_t, &jpg->tail_data, tail_data_len);
			memset(jpg->tail_data, 0, tail_data_len);
		}
	}
	jpg->has_zero_padding_bit = (int) microjxl__brd_read(&b, 1);
	if (jpg->has_zero_padding_bit) {
		uint32_t nbit = microjxl__brd_read(&b, 24);
		if (nbit > 1u << 24) MICROJXL__RAISE("jbrd");
		jpg->num_padding_bits = nbit;
		if (nbit) {
			uint32_t j;
			MICROJXL__TRY_MALLOC(uint8_t, &jpg->padding_bits, nbit);
			for (j = 0; j < nbit; ++j) jpg->padding_bits[j] = (uint8_t) microjxl__brd_read(&b, 1);
		}
	}
	if (b.overread) MICROJXL__RAISE("jbrd");

	/* marker-order consistency check (libjxl VisitFields tail) */
	{
		size_t dht_index = 0, scan_index = 0, mi;
		int is_progressive = 0;
		int ac_ok[MICROJXL__BRD_MAX_HUFF_TABLES] = {0};
		int dc_ok[MICROJXL__BRD_MAX_HUFF_TABLES] = {0};
		for (mi = 0; mi < jpg->num_markers; ++mi) {
			uint8_t m = jpg->marker_order[mi];
			if (m == 0xC2) {
				is_progressive = 1;
			} else if (m == 0xC4) {
				for (; dht_index < (size_t) jpg->num_huffman;) {
					const microjxl__brd_huffman *huff = &jpg->huffman[dht_index++];
					int index = huff->slot_id;
					if (index & 0x10) { index -= 0x10; ac_ok[index] = 1; }
					else dc_ok[index] = 1;
					if (huff->is_last) break;
				}
			} else if (m == 0xDA) {
				const microjxl__brd_scan *si = &jpg->scans[scan_index++];
				int32_t ci;
				for (ci = 0; ci < si->num_components; ++ci) {
					int want_dc = !is_progressive || (si->Ss == 0);
					int want_ac = !is_progressive || (si->Ss != 0) || (si->Se != 0);
					if (want_dc && !dc_ok[si->comps[ci].dc_tbl_idx]) MICROJXL__RAISE("jbrd");
					if (want_ac && !ac_ok[si->comps[ci].ac_tbl_idx]) MICROJXL__RAISE("jbrd");
				}
			}
		}
	}
	*bitpos_out = b.bitpos;
	return 0;
MICROJXL__ON_ERROR:
	microjxl__brd_free_jpegdata(jpg);
	return st->err;
}

/* fills the brotli-compressed payloads (APPn unknowns, COM, intermarker,
 * tail) from the brotli stream that follows the bit-packed fields;
 * mirrors libjxl DecodeJPEGData */
MICROJXL_STATIC int microjxl__brd_fill_payloads(
	microjxl__st *st, microjxl__brd_payload *pl, microjxl__brd_jpegdata *jpg,
	const uint8_t *icc, size_t icc_size,
	const uint8_t *exif, size_t exif_size, const uint8_t *xmp, size_t xmp_size
) {
	static const uint8_t kIccProfileTag[12] = "ICC_PROFILE";
	static const uint8_t kExifTag[6] = "Exif\0";
	static const uint8_t kXMPTag[29] = "http://ns.adobe.com/xap/1.0/";
	int i;
	size_t num_icc = 0;
	(void) st;
	for (i = 0; i < jpg->num_app_markers; ++i) {
		uint8_t *marker = jpg->app_data[i];
		size_t size = jpg->app_size[i];
		if (size < 3) return 1;
		if (jpg->app_type[i] != 0) {
			size_t size_minus_1 = size - 1;
			marker[1] = (uint8_t) (size_minus_1 >> 8);
			marker[2] = (uint8_t) (size_minus_1 & 0xFF);
			if (jpg->app_type[i] == 1) { // kICC
				if (size < 17) return 1;
				marker[0] = 0xE2;
				memcpy(&marker[3], kIccProfileTag, sizeof kIccProfileTag);
				marker[15] = (uint8_t) ++num_icc;
			}
		}
	}
	for (i = 0; i < jpg->num_app_markers; ++i) {
		uint8_t *marker = jpg->app_data[i];
		size_t size = jpg->app_size[i];
		if (jpg->app_type[i] == 1) { // kICC: total count in [16]
			marker[16] = (uint8_t) num_icc;
		} else if (jpg->app_type[i] == 2) { // kExif
			if (size < 3 + sizeof kExifTag) return 1;
			marker[0] = 0xE1;
			memcpy(&marker[3], kExifTag, sizeof kExifTag);
		} else if (jpg->app_type[i] == 3) { // kXMP
			if (size < 3 + sizeof kXMPTag) return 1;
			marker[0] = 0xE1;
			memcpy(&marker[3], kXMPTag, sizeof kXMPTag);
		}
	}
	/* Inject the ICC profile (libjxl jpeg::SetJPEGDataFromICC): the profile
	 * bytes are copied sequentially into the kICC markers' payloads; the
	 * total must match exactly. Exif/XMP (libjxl JxlToJpegDecoder::SetExif/
	 * SetXmp): the container's Exif box content already had its 4-byte TIFF
	 * header stripped by the box capture; the payload follows the tag. */
	{
		size_t icc_pos = 0;
		for (i = 0; i < jpg->num_app_markers; ++i) {
			uint8_t *marker = jpg->app_data[i];
			size_t size = jpg->app_size[i];
			if (jpg->app_type[i] == 1) {
				size_t len = size - 17;
				if (icc_pos + len > icc_size) return 1;
				memcpy(&marker[17], icc + icc_pos, len);
				icc_pos += len;
			}
		}
		if (icc_pos != icc_size && icc_pos != 0) return 1;
	}
	for (i = 0; i < jpg->num_app_markers; ++i) {
		uint8_t *marker = jpg->app_data[i];
		size_t size = jpg->app_size[i];
		if (jpg->app_type[i] == 2) {
			if (size != exif_size + 3 + sizeof kExifTag) return 1;
			memcpy(&marker[3 + sizeof kExifTag], exif, exif_size);
		} else if (jpg->app_type[i] == 3) {
			if (size != xmp_size + 3 + sizeof kXMPTag) return 1;
			memcpy(&marker[3 + sizeof kXMPTag], xmp, xmp_size);
		}
	}
	/* brotli-fill the unknown APP markers */
	for (i = 0; i < jpg->num_app_markers; ++i) {
		if (jpg->app_type[i] != 0) continue;
		if (microjxl__brd_payload_read(pl, jpg->app_data[i], jpg->app_size[i])) return 1;
		if ((size_t) jpg->app_data[i][1] * 256u + jpg->app_data[i][2] + 1u != jpg->app_size[i]) return 1;
	}
	for (i = 0; i < jpg->num_com_markers; ++i) {
		if (microjxl__brd_payload_read(pl, jpg->com_data[i], jpg->com_size[i])) return 1;
		if ((size_t) jpg->com_data[i][1] * 256u + jpg->com_data[i][2] + 1u != jpg->com_size[i]) return 1;
	}
	for (i = 0; i < jpg->num_intermarker; ++i) {
		if (microjxl__brd_payload_read(pl, jpg->inter_marker_data[i], jpg->inter_marker_size[i])) return 1;
	}
	if (microjxl__brd_payload_read(pl, jpg->tail_data, jpg->tail_size)) return 1;
	if (pl->out_pos != pl->out_cap) return 1; // excess data
	return 0;
}

/* ---- JPEG serializer (libjxl WriteJpeg): walks marker_order ---- */
MICROJXL_STATIC int microjxl__brd_write_jpeg(
	microjxl__brd_jpegdata *jpg, const uint8_t *pad_bits, size_t num_pad_bits,
	microjxl__brd_buffer *out
) {
	microjxl__brd_bwriter bw;
	microjxl__brd_hufftable dc_table[MICROJXL__BRD_MAX_HUFF_TABLES];
	microjxl__brd_hufftable ac_table[MICROJXL__BRD_MAX_HUFF_TABLES];
	size_t dht_index = 0, dqt_index = 0, app_index = 0, com_index = 0, data_index = 0;
	int scan_index = 0, is_progressive = 0, seen_dri = 0, mi;
	int32_t last_dc_coeff[4] = {0};
	uint8_t hdr[2];

	memset(dc_table, 0, sizeof(dc_table));
	memset(ac_table, 0, sizeof(ac_table));

	/* SOI */
	hdr[0] = 0xFF; hdr[1] = 0xD8;
	microjxl__brd_put(out, hdr, 2);

	for (mi = 0; mi < (int) jpg->num_markers; ++mi) {
		uint8_t marker = jpg->marker_order[mi];
		if (marker <= 0xC2) is_progressive = (marker == 0xC2);
		switch (marker) {
			case 0xC0: case 0xC1: case 0xC2: case 0xC9: case 0xCA: { // SOF
				uint8_t data[2 + 8 + 3 * 3];
				size_t pos = 0;
				size_t n_comps = (size_t) jpg->num_components;
				size_t marker_len = 8 + 3 * n_comps;
				int ci2;
				data[pos++] = 0xFF; data[pos++] = marker;
				data[pos++] = (uint8_t) (marker_len >> 8); data[pos++] = (uint8_t) (marker_len & 0xFF);
				data[pos++] = 8; // precision
				data[pos++] = (uint8_t) (jpg->height >> 8); data[pos++] = (uint8_t) (jpg->height & 0xFF);
				data[pos++] = (uint8_t) (jpg->width >> 8); data[pos++] = (uint8_t) (jpg->width & 0xFF);
				data[pos++] = (uint8_t) n_comps;
				for (ci2 = 0; ci2 < jpg->num_components; ++ci2) {
					data[pos++] = (uint8_t) jpg->components[ci2].id;
					data[pos++] = (uint8_t) ((jpg->components[ci2].h_samp_factor << 4) | jpg->components[ci2].v_samp_factor);
					data[pos++] = (uint8_t) jpg->quant[jpg->components[ci2].quant_idx].index;
				}
				microjxl__brd_put(out, data, pos);
				break;
			}
			case 0xC4: { // DHT
				uint8_t counts_u8[17];
				size_t marker_len;
				int hdrpos = 0;
				uint8_t hdr4[4];
				marker_len = 2;
				{
					size_t i2;
					for (i2 = dht_index; i2 < (size_t) jpg->num_huffman; ++i2) {
						const microjxl__brd_huffman *huff = &jpg->huffman[i2];
						int32_t j;
						for (j = 0; j <= 16; ++j) marker_len += (size_t) huff->counts[j];
						if (marker_len == 2) break; // empty DHT marker
						marker_len += MICROJXL__BRD_MAX_BIT_LENGTH;
						if (huff->is_last) break;
					}
				}
				hdr4[hdrpos++] = 0xFF; hdr4[hdrpos++] = 0xC4;
				hdr4[hdrpos++] = (uint8_t) (marker_len >> 8); hdr4[hdrpos++] = (uint8_t) (marker_len & 0xFF);
				microjxl__brd_put(out, hdr4, 4);
				for (;;) {
					microjxl__brd_huffman *huff;
					microjxl__brd_hufftable *table;
					int index, max_length = 0, j;
					int32_t total_count = 0;
					if (dht_index >= (size_t) jpg->num_huffman) return 1;
					huff = &jpg->huffman[dht_index++];
					index = huff->slot_id;
					for (j = 0; j <= 16; ++j) {
						if (huff->counts[j] != 0) max_length = j;
						total_count += huff->counts[j];
					}
					if (total_count == 0) break; // empty DHT marker
					if (index & 0x10) { index -= 0x10; table = &ac_table[index]; }
					else table = &dc_table[index];
					{
						int32_t j;
						for (j = 0; j < MICROJXL__BRD_ALPHABET_SIZE; ++j) { table->depth[j] = 127; table->code[j] = 0; }
					}
					if (!microjxl__brd_build_hufftable(huff, table)) return 1;
					table->initialized = 1;
					--total_count;
					counts_u8[0] = (uint8_t) huff->slot_id;
					for (j = 1; j <= MICROJXL__BRD_MAX_BIT_LENGTH; ++j)
						counts_u8[j] = (uint8_t) (j == max_length ? huff->counts[j] - 1 : huff->counts[j]);
					microjxl__brd_put(out, counts_u8, 17);
					for (j = 0; j < total_count; ++j) {
						uint8_t v = (uint8_t) huff->values[j];
						microjxl__brd_put(out, &v, 1);
					}
					if (huff->is_last) break;
				}
				break;
			}
			case 0xDB: { // DQT
				size_t marker_len;
				uint8_t hdr4[4];
				int hdrpos = 0;
				marker_len = 2;
				{
					size_t i2;
					for (i2 = dqt_index; i2 < (size_t) jpg->num_quant; ++i2) {
						const microjxl__brd_quant *table = &jpg->quant[i2];
						marker_len += 1 + (size_t) (table->precision ? 2 : 1) * MICROJXL__BRD_DCT_BLOCK_SIZE;
						if (table->is_last) break;
					}
				}
				hdr4[hdrpos++] = 0xFF; hdr4[hdrpos++] = 0xDB;
				hdr4[hdrpos++] = (uint8_t) (marker_len >> 8); hdr4[hdrpos++] = (uint8_t) (marker_len & 0xFF);
				microjxl__brd_put(out, hdr4, 4);
				for (;;) {
					microjxl__brd_quant *table;
					uint8_t pth;
					int i2;
					if (dqt_index >= (size_t) jpg->num_quant) return 1;
					table = &jpg->quant[dqt_index++];
					pth = (uint8_t) ((table->precision << 4) + table->index);
					microjxl__brd_put(out, &pth, 1);
					for (i2 = 0; i2 < MICROJXL__BRD_DCT_BLOCK_SIZE; ++i2) {
						int val_idx = (int) MICROJXL__BRD_NATURAL_ORDER[i2];
						int val = table->values[val_idx];
						if (table->precision) {
							uint8_t b2[2] = { (uint8_t) (val >> 8), (uint8_t) (val & 0xFF) };
							microjxl__brd_put(out, b2, 2);
						} else {
							uint8_t b1 = (uint8_t) (val & 0xFF);
							microjxl__brd_put(out, &b1, 1);
						}
					}
					if (table->is_last) break;
				}
				break;
			}
			case 0xDD: { // DRI
				uint8_t dri[6] = { 0xFF, 0xDD, 0, 4,
					(uint8_t) (jpg->restart_interval >> 8), (uint8_t) (jpg->restart_interval & 0xFF) };
				seen_dri = 1;
				microjxl__brd_put(out, dri, 6);
				break;
			}
			case 0xD0: case 0xD1: case 0xD2: case 0xD3:
			case 0xD4: case 0xD5: case 0xD6: case 0xD7: { // RSTn
				microjxl__brd_emit_marker(&bw, marker);
				break;
			}
			case 0xD9: { // EOI + tail data
				microjxl__brd_put(out, (const uint8_t *) "\xFF\xD9", 2);
				if (jpg->tail_size) microjxl__brd_put(out, jpg->tail_data, jpg->tail_size);
				break;
			}
			case 0xDA: { // SOS + entropy-coded scan
				microjxl__brd_scan *scan = &jpg->scans[scan_index];
				uint8_t data[2 + 6 + 2 * 4];
				size_t pos = 0;
				size_t n_scans = (size_t) scan->num_components;
				size_t marker_len = 6 + 2 * n_scans;
				int restart_interval = seen_dri ? jpg->restart_interval : 0;
				int mcus_per_row, mcu_rows, mcu_x, mcu_y;
				int Al = is_progressive ? scan->Al : 0;
				int Ss = is_progressive ? scan->Ss : 0;
				int Se = is_progressive ? scan->Se : 63;
				int want_ac = (Ss != 0) || (Se != 0);
				int want_dc = (Ss == 0);
				int is_interleaved = (scan->num_components > 1);
				microjxl__brd_coding_state coding_state;
				int ezr_pos = 0, next_ezr, reset_pos = 0, next_reset, restarts_to_go, next_restart_marker;
				long block_scan_index = 0;
				int ci2;
				(void) want_dc;
				data[pos++] = 0xFF; data[pos++] = 0xDA;
				data[pos++] = (uint8_t) (marker_len >> 8); data[pos++] = (uint8_t) (marker_len & 0xFF);
				data[pos++] = (uint8_t) n_scans;
				for (ci2 = 0; ci2 < scan->num_components; ++ci2) {
					data[pos++] = (uint8_t) jpg->components[scan->comps[ci2].comp_idx].id;
					data[pos++] = (uint8_t) ((scan->comps[ci2].dc_tbl_idx << 4) + scan->comps[ci2].ac_tbl_idx);
				}
				data[pos++] = (uint8_t) scan->Ss;
				data[pos++] = (uint8_t) scan->Se;
				data[pos++] = (uint8_t) ((scan->Ah << 4) | scan->Al);
				microjxl__brd_put(out, data, pos);

				microjxl__brd_bw_init(&bw, out);
				memset(&coding_state, 0, sizeof(coding_state));
				coding_state.eob_run = 0;
				restarts_to_go = restart_interval;
				next_restart_marker = 0;
				next_ezr = ezr_pos < scan->num_extra_zero_runs ? scan->ezr_block_idx[ezr_pos] : -1;
				next_reset = reset_pos < scan->num_reset_points ? scan->reset_points[reset_pos] : -1;
				memset(last_dc_coeff, 0, sizeof(last_dc_coeff));
				microjxl__brd_calculate_mcu_size(jpg, scan, &mcus_per_row, &mcu_rows);
				for (mcu_y = 0; mcu_y < mcu_rows; ++mcu_y) {
					for (mcu_x = 0; mcu_x < mcus_per_row; ++mcu_x) {
						if (restart_interval > 0 && restarts_to_go == 0) {
							microjxl__brd_flush(&coding_state, &bw);
							microjxl__brd_jump_to_byte(&bw);
							microjxl__brd_emit_marker(&bw, 0xD0 + next_restart_marker);
							next_restart_marker += 1;
							next_restart_marker &= 0x7;
							restarts_to_go = restart_interval;
							memset(last_dc_coeff, 0, sizeof(last_dc_coeff));
						}
						for (ci2 = 0; ci2 < scan->num_components; ++ci2) {
							microjxl__brd_comp_scan *si = &scan->comps[ci2];
							microjxl__brd_component *c = &jpg->components[si->comp_idx];
							microjxl__brd_hufftable *dc_huff = &dc_table[si->dc_tbl_idx];
							microjxl__brd_hufftable *ac_huff = &ac_table[si->ac_tbl_idx];
							int n_blocks_y = is_interleaved ? c->v_samp_factor : 1;
							int n_blocks_x = is_interleaved ? c->h_samp_factor : 1;
							int iy, ix;
							if (want_ac && !ac_huff->initialized) return 1;
							for (iy = 0; iy < n_blocks_y; ++iy) {
								for (ix = 0; ix < n_blocks_x; ++ix) {
									int block_y = mcu_y * n_blocks_y + iy;
									int block_x = mcu_x * n_blocks_x + ix;
									size_t block_idx = (size_t) block_y * (size_t) c->width_in_blocks + (size_t) block_x;
									const int16_t *coeffs = c->coeffs + (block_idx << 6);
									int num_zero_runs = 0;
									int ok;
									if ((long) block_scan_index == (long) next_reset) {
										microjxl__brd_flush(&coding_state, &bw);
										next_reset = reset_pos < scan->num_reset_points ? scan->reset_points[reset_pos++] : -1;
									}
									if ((long) block_scan_index == (long) next_ezr) {
										num_zero_runs = scan->ezr_count[ezr_pos];
										++ezr_pos;
										next_ezr = ezr_pos < scan->num_extra_zero_runs ? scan->ezr_block_idx[ezr_pos] : -1;
									}
									if (!is_progressive) {
										ok = microjxl__brd_encode_dct_block_sequential(coeffs, dc_huff, ac_huff, num_zero_runs, &last_dc_coeff[si->comp_idx], &bw);
									} else if (scan->Ah == 0) {
										ok = microjxl__brd_encode_dct_block_progressive(coeffs, dc_huff, ac_huff, Ss, Se, Al, num_zero_runs, &coding_state, &last_dc_coeff[si->comp_idx], &bw);
									} else {
										ok = microjxl__brd_encode_refinement_bits(coeffs, ac_huff, Ss, Se, Al, &coding_state, &bw);
									}
									if (!ok) return 1;
									++block_scan_index;
								}
							}
						}
						--restarts_to_go;
					}
				}
				microjxl__brd_flush(&coding_state, &bw);
				microjxl__brd_jump_to_byte(&bw);
				if (!bw.healthy) return 1;
				++scan_index;
				break;
			}
			case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4:
			case 0xE5: case 0xE6: case 0xE7: case 0xE8: case 0xE9:
			case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF: { // APPn
				size_t app_index2 = app_index++;
				uint8_t ff = 0xFF;
				if (app_index2 >= (size_t) jpg->num_app_markers) return 1;
				microjxl__brd_put(out, &ff, 1);
				microjxl__brd_put(out, jpg->app_data[app_index2], jpg->app_size[app_index2]);
				break;
			}
			case 0xFE: { // COM
				size_t com_index2 = com_index++;
				uint8_t ff = 0xFF;
				if (com_index2 >= (size_t) jpg->num_com_markers) return 1;
				microjxl__brd_put(out, &ff, 1);
				microjxl__brd_put(out, jpg->com_data[com_index2], jpg->com_size[com_index2]);
				break;
			}
			case 0xFF: { // intermarker data
				size_t data_index2 = data_index++;
				if (data_index2 >= (size_t) jpg->num_intermarker) return 1;
				if (jpg->inter_marker_size[data_index2])
					microjxl__brd_put(out, jpg->inter_marker_data[data_index2], jpg->inter_marker_size[data_index2]);
				break;
			}
			default:
				return 1;
			}
		}
		(void) pad_bits; (void) num_pad_bits;
		return 0;
	}

/* ---- top-level jbrd driver ----
 * Assembles the reconstructed JPEG from the container's jbrd payload and
 * the captured coefficient domain. Returns 0 on success (*out/*out_size
 * receive a MICROJXL_MALLOC'd buffer owned by the caller). */
/* brotli-decompresses the stream that follows the jbrd bit-packed fields.
 * The bit reader consumed `bitpos` bits; the brotli stream starts at the
 * next byte boundary. Output size is unknown up-front: grown in chunks. */
MICROJXL_STATIC int microjxl__brd_decompress_payload(
	microjxl__st *st, const uint8_t *enc, size_t enc_size, size_t bitpos, microjxl__brd_payload *pl
) {
	size_t byte = bitpos >> 3;
	const uint8_t *src;
	size_t src_size, cap = 4096;
	size_t produced;
	(void) st;
	memset(pl, 0, sizeof(*pl));
	if (byte >= enc_size) return 1;
	/* jump to byte boundary: libjxl discards the remainder bits */
	if (bitpos & 7) byte++;
	if (byte >= enc_size) return 1;
	src = enc + byte;
	src_size = enc_size - byte;
	for (;;) {
		uint8_t *outbuf = (uint8_t *) MICROJXL_MALLOC(cap);
		if (!outbuf) return 1;
		produced = microjxl__br_decompress(src, src_size, outbuf, cap);
		if (produced != (size_t) -1) {
			pl->out = outbuf; pl->out_cap = produced; pl->out_pos = 0;
			pl->in = src; pl->in_size = src_size; pl->in_pos = src_size;
			return 0;
		}
		microjxl__mem_free(outbuf);
		if (cap > (1 << 28)) return 1; // too large
		cap <<= 1; // NEEDS_MORE_OUTPUT: retry with a larger buffer
	}
}

/* Top-level JPEG reconstruction (Part 2 §9.10, libjxl decode_to_jpeg.cc +
 * jpeg/dec_jpeg_data.cc): decodes the container's jbrd payload (the
 * JPEGData bitstream starting at the is_gray bit, byte-padded, followed by
 * a brotli stream of the APPn/COM/intermarker/tail payloads), merges the
 * captured coefficient domain (f->jpeg_coeffs AC + f->jpeg_dcv DC) and the
 * recovered RAW quant tables, and serializes the JPEG bytes.
 * Returns 0 on success (*out/*out_size receive a MICROJXL_MALLOC'd buffer
 * owned by the caller). */
MICROJXL_STATIC int microjxl__jpeg_reconstruct(
	microjxl__st *st, const uint8_t *jbrd_payload, size_t jbrd_payload_size,
	const uint8_t *icc, size_t icc_size,
	const uint8_t *exif, size_t exif_size, const uint8_t *xmp, size_t xmp_size,
	uint8_t **out, size_t *out_size
) {
	microjxl__brd_jpegdata jpg;
	microjxl__brd_payload pl;
	microjxl__brd_buffer obuf;
	microjxl__frame_st *f = st->frame;
	int32_t W = st->image->width, H = st->image->height;
	int i, ret = 1;
	/* JPEG channel order: libjxl's JpegOrder(color_transform, is_gray) —
	 * is_gray comes from the jbrd bitstream (first bit), not from the frame
	 * (libjxl FrameDecoder::InitFrameOutput uses
	 * JpegOrder(ColorTransform::kYCbCr, num_components == 1) and a gray
	 * image may legitimately carry the alternate=YCbCr flag). The map is
	 * per channel: JPEG component that channel c of the frame feeds. */
	int jpeg_c_map[3] = {0, 0, 0};
	int chan_for_comp[3] = {0, 0, 0};
	int is_gray;
	int32_t dcoff[3] = {0};
	int32_t Wb = microjxl__ceil_div32(W, 8), Hb = microjxl__ceil_div32(H, 8);
	const int32_t *qtable = f->jpeg_qtable;

	*out = NULL; *out_size = 0;
	memset(&obuf, 0, sizeof(obuf));
	if (!f->jpeg_qtable_ok || !f->jpeg_recon_alloc) return 1;

	size_t brd_bitpos = 0;
	if (microjxl__brd_parse(st, jbrd_payload, jbrd_payload_size, &jpg, &brd_bitpos)) {
#ifdef MICROJXL_DEBUG_JPEG_TRACE
		fprintf(stderr, "[jbrd] parse failed (payload %zu bytes)\n", jbrd_payload_size);
#endif
		return 1;
	}
#ifdef MICROJXL_DEBUG_JPEG_TRACE
	fprintf(stderr, "[jbrd] parse ok bitpos=%zu gray=%d ncomp=%d nquant=%d nhuff=%d nscan=%d\n",
		brd_bitpos, (int) jpg.is_gray, (int) jpg.num_components, (int) jpg.num_quant, (int) jpg.num_huffman, (int) jpg.num_scans);
#endif
	is_gray = jpg.is_gray;
	if (is_gray != (jpg.num_components == 1)) goto fail;
	if (is_gray != (f->do_ycbcr == 0 && st->image->cspace == MICROJXL__CS_GREY) &&
		!is_gray) {
		/* colour: the jbrd component count must match the frame's channels */
		goto fail;
	}
	if (is_gray) { jpeg_c_map[0] = jpeg_c_map[1] = jpeg_c_map[2] = 0; } // {{0,0,0}}
	else if (f->do_ycbcr) { jpeg_c_map[0] = 1; jpeg_c_map[1] = 0; jpeg_c_map[2] = 2; } // kYCbCr: {{1,0,2}}
	else { jpeg_c_map[0] = 0; jpeg_c_map[1] = 1; jpeg_c_map[2] = 2; } // kNone: {{0,1,2}}
	/* inverse map: XYB capture channel that feeds JPEG component i
	 * (libjxl's jbrd loops iterate c = 1,0,2 and write jpeg_row[c] =
	 * component[jpeg_c_map[c]]; gray propagates channel 1 only). */
	if (is_gray) { chan_for_comp[0] = chan_for_comp[1] = chan_for_comp[2] = 1; }
	else if (f->do_ycbcr) { chan_for_comp[0] = 1; chan_for_comp[1] = 0; chan_for_comp[2] = 2; }
	else { chan_for_comp[0] = 0; chan_for_comp[1] = 1; chan_for_comp[2] = 2; }

	jpg.width = W;
	jpg.height = H;
	/* libjxl dec_frame.cc InitFrameOutput: width_in_blocks =
	 * DivCeil(xsize, 8 << max_hshift) << max_hshift >> HShift(c);
	 * (microjxl stores the EFFECTIVE shift; RawHShift = maxh - eff). */
	{
		int32_t maxh = 0, maxv = 0;
		for (i = 0; i < 3; ++i) {
			if (f->jpeg_hshift[i] > maxh) maxh = f->jpeg_hshift[i];
			if (f->jpeg_vshift[i] > maxv) maxv = f->jpeg_vshift[i];
		}
		for (i = 0; i < 3; ++i) {
			int32_t c = chan_for_comp[i]; // XYB channel feeding this component
			int32_t xblocks = microjxl__ceil_div32(W, 8 << maxh);
			int32_t yblocks = microjxl__ceil_div32(H, 8 << maxv);
			jpg.components[i].width_in_blocks =
				microjxl__ceil_div32(xblocks << maxh, 1 << f->jpeg_hshift[c]);
			jpg.components[i].height_in_blocks =
				microjxl__ceil_div32(yblocks << maxv, 1 << f->jpeg_vshift[c]);
			/* keep the un-shifted padded grid too: jpeg_wib matches libjxl's
			 * per-component width only after the shift, but the coefficient
			 * grid captured in dequant_hf uses the same formula, so they
			 * agree by construction. */
			jpg.components[i].h_samp_factor = 1 << (maxh - f->jpeg_hshift[c]);
			jpg.components[i].v_samp_factor = 1 << (maxv - f->jpeg_vshift[c]);
		}
		(void) Wb; (void) Hb;
	}
	for (i = 0; i < 3; ++i) {
		int32_t c = chan_for_comp[i]; // capture arrays (jpeg_coeffs/jpeg_dcv) are XYB-indexed
		if (!f->jpeg_coeffs[c]) continue; // gray: only channel 1 is captured
		jpg.components[i].width_in_blocks = f->jpeg_wib[c];
		jpg.components[i].height_in_blocks = f->jpeg_hib[c];
		jpg.components[i].coeffs_cap = (size_t) jpg.components[i].width_in_blocks * (size_t) jpg.components[i].height_in_blocks * MICROJXL__BRD_DCT_BLOCK_SIZE;
		jpg.components[i].coeffs = (int16_t *) MICROJXL_MALLOC(jpg.components[i].coeffs_cap * sizeof(int16_t));
		if (!jpg.components[i].coeffs) goto fail;
		memcpy(jpg.components[i].coeffs, f->jpeg_coeffs[c], jpg.components[i].coeffs_cap * sizeof(int16_t));
	}

	/* fill the JPEG quant tables (libjxl dec_frame.cc ProcessACGlobal):
	 * quant[qpos].values[x*8+y] = qtable[quant_c*64 + y*8 + x]; for gray,
	 * quant_c = 1 (the first table). */
	{
		int qt_set = 0;
		for (i = 0; i < jpg.num_components; ++i) {
			/* dec_frame.cc:495: quant table for component jpeg_c_map[c] comes
			 * from XYB channel c — i.e. component i uses chan_for_comp[i]
			 * (which is 1 for every component of a gray image). */
			int quant_c = chan_for_comp[i];
			int qpos = jpg.components[i].quant_idx;
			int x, y;
			if (qpos >= jpg.num_quant) goto fail;
			qt_set |= 1 << qpos;
			for (x = 0; x < 8; ++x) {
				for (y = 0; y < 8; ++y) {
					jpg.quant[qpos].values[x * 8 + y] = qtable[quant_c * 64 + y * 8 + x];
				}
			}
		}
		for (i = 0; i < jpg.num_quant; ++i) {
			int j;
			if (qt_set & (1 << i)) continue;
			if (i == 0) goto fail; // first quant table unused
			for (j = 0; j < 64; ++j) jpg.quant[i].values[j] = jpg.quant[i - 1].values[j];
		}
	}

	/* DC offsets (libjxl dec_group.cc: only for ColorTransform::kNone) and
	 * the per-channel DC patch: jpeg_dc = Clamp1<float>(dc_rows[c][sbx] -
	 * dcoff[c], -2047, 2047) truncated toward zero, read at the channel's
	 * subsampled block coordinates. dc_rows = raw * 2^-ep (ClearDCMul). */
	for (i = 0; i < 3; ++i) {
		if (!f->do_ycbcr && !is_gray) dcoff[i] = 1024 / qtable[64 * i]; // kNone
	}
	for (i = 0; i < (is_gray ? 1 : 3); ++i) {
		int c = chan_for_comp[i]; // XYB capture channel feeding component i
		int32_t hshift = f->jpeg_hshift[c], vshift = f->jpeg_vshift[c];
		int32_t jcomp = i;
		int32_t dcw = f->jpeg_dcv_w[c], dch = f->jpeg_dcv_h[c];
		int32_t prec = f->jpeg_dc_prec_shift;
		int32_t wibc = jpg.components[jcomp].width_in_blocks;
		int32_t hibc = jpg.components[jcomp].height_in_blocks;
		(void) hshift; (void) vshift; (void) dch; // dcv grid is already per-channel
		int32_t sbx, sby;
		(void) dch;
		for (sby = 0; sby < hibc; ++sby) {
			for (sbx = 0; sbx < wibc; ++sbx) {
				int32_t raw = f->jpeg_dcv[c][(size_t) sby * dcw + sbx];
				float dcf = prec ? (float) raw / (float) (1 << prec) : (float) raw;
				/* Clamp1<float> then C cast to int16: truncation toward zero */
				float fv = dcf - (float) dcoff[c];
				int32_t v;
				if (fv > 2047.0f) v = 2047;
				else if (fv < -2047.0f) v = -2047;
				else v = (int32_t) fv;
				jpg.components[jcomp].coeffs[((size_t) sby * (size_t) wibc + (size_t) sbx) * 64] = (int16_t) v;
			}
		}
	}

	/* brotli-decode the trailing payloads */
#ifdef MICROJXL_DEBUG_JPEG_TRACE
	fprintf(stderr, "[jbrd] dcv[0..7]:");
	for (i = 0; i < 8; ++i) fprintf(stderr, " %d", f->jpeg_dcv[chan_for_comp[0]][i]);
	fprintf(stderr, "\n[jbrd] coeff0[0..15] (after DC patch):");
	for (i = 0; i < 16; ++i) fprintf(stderr, " %d", (int) jpg.components[0].coeffs[i]);
	fprintf(stderr, "\n");
#endif
	memset(&pl, 0, sizeof(pl));
	if (microjxl__brd_decompress_payload(st, jbrd_payload, jbrd_payload_size, brd_bitpos, &pl)) {
#ifdef MICROJXL_DEBUG_JPEG_TRACE
		fprintf(stderr, "[jbrd] brotli decompress failed at bitpos=%zu\n", brd_bitpos);
#endif
		goto fail;
	}
	{
		int pfail = microjxl__brd_fill_payloads(st, &pl, &jpg, icc, icc_size, exif, exif_size, xmp, xmp_size);
		microjxl__mem_free(pl.out);
		if (pfail) goto fail;
	}

	/* serialize */
	if (microjxl__brd_write_jpeg(&jpg, NULL, 0, &obuf) || obuf.failed || !obuf.size) {
#ifdef MICROJXL_DEBUG_JPEG_TRACE
		fprintf(stderr, "[jbrd] write_jpeg failed (ret=%d failed=%d size=%zu)\n", ret, (int) obuf.failed, obuf.size);
#endif
		goto fail;
	}
	microjxl__brd_free_jpegdata(&jpg);
	*out = obuf.data;
	*out_size = obuf.size;
	return 0;
fail:
	microjxl__brd_free_jpegdata(&jpg);
	microjxl__mem_free(obuf.data);
	return ret;
}

/* advanced() hook: builds the reconstruction from the container's jbrd box
 * and the frame capture (f->jpeg_*), caching the bytes in inner. */
MICROJXL_STATIC uint8_t *microjxl__jpeg_reconstruct_from_frame(
	microjxl__st *st, size_t *out_size
) {
	microjxl__image_st *im = st->image;
	microjxl__container_st *c = st->container;
	uint8_t *out = NULL;
	size_t size = 0;
	*out_size = 0;
	if (!c->jbrd || !c->jbrd_size) return NULL;
	if (microjxl__jpeg_reconstruct(st, c->jbrd, c->jbrd_size,
			(const uint8_t *) im->icc, im->iccsize,
			c->exif, c->exif_size, c->xmp, c->xmp_size,
			&out, &size)) {
		return NULL;
	}
	*out_size = size;
	return out;
}

////////////////////////////////////////////////////////////////////////////////
// LfGroup: downsampled LF image (optionally smoothed), varblock information

typedef struct {
	int32_t coeffoff_qfidx; // offset to coeffs (always a multiple of 64) | qf index (always < 16)
	union {
		int32_t m1; // HfMul - 1 during microjxl__hf_metadata, to avoid overflow at this stage
		float inv; // 1 / HfMul after microjxl__hf_metadata
	} hfmul;
	int32_t row_quant; // HfMul; the only field valid after microjxl__hf_metadata (m1/inv overlap)
	// DctSelect is embedded in blocks
} microjxl__varblock;

typedef struct microjxl__lf_group_st {
	int64_t idx;

	int32_t left, top;
	int32_t width, height; // <= 8192
	int32_t width8, height8; // <= 1024
	int32_t width64, height64; // <= 128

	// contained group indices: [gidx + gstride * y, gidx + gstride * y + gcolumns) for each row
	int64_t gidx, grows, gcolumns, gstride;

	microjxl__plane xfromy, bfromy; // width64 x height64 each
	microjxl__plane sharpness; // width8 x height8

	int32_t nb_varblocks; // <= 2^20 (TODO spec issue: named nb_blocks)
	// bits 0..19: varblock index [0, nb_varblocks)
	// bits 20..24: DctSelect + 2, or 1 if not the top-left corner (0 is reserved for unused block)
	microjxl__plane blocks; // width8 x height8
	microjxl__varblock *varblocks; // [nb_varblocks]

	float *llfcoeffs[3]; // [width8*height8] each
	// TODO coeffs can be integers before dequantization
	float *coeffs[3]; // [width8*height8*64] each, aligned
	#define MICROJXL__COEFFS_ALIGN 64
	uint8_t coeffs_misalign[3];

	// precomputed lf_idx
	microjxl__plane lfindices; // [width8*height8]

	int loaded;
} microjxl__lf_group_st;

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_quant(
	microjxl__st *st, int32_t extra_prec, microjxl__modular *m, microjxl__lf_group_st *gg, microjxl__plane outlfquant[3]
);
MICROJXL_STATIC void microjxl__jpeg_recon_capture_dcv(
	microjxl__st *st, const microjxl__modular *m, int32_t extra_prec,
	const microjxl__lf_group_st *gg
);
MICROJXL__STATIC_RETURNS_ERR microjxl__hf_metadata(
	microjxl__st *st, int32_t nb_varblocks,
	microjxl__modular *m, const microjxl__plane lfquant[3], microjxl__lf_group_st *gg
);
MICROJXL__STATIC_RETURNS_ERR microjxl__lf_group(microjxl__st *st, microjxl__lf_group_st *gg);
MICROJXL_STATIC void microjxl__mem_free_lf_group(microjxl__lf_group_st *gg);

// ----------------------------------------
// recursion for LF dequantization operations
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING 400
#define MICROJXL__P 16
#include MICROJXL_FILENAME
#define MICROJXL__P 32
#include MICROJXL_FILENAME
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING (-1)

#endif // MICROJXL__RECURSING < 0
#if MICROJXL__RECURSING == 400
	#define microjxl__intP MICROJXL__CONCAT3(int, MICROJXL__P, _t)
	#define MICROJXL__PIXELS MICROJXL__CONCAT3(MICROJXL__I, MICROJXL__P, _PIXELS)
// ----------------------------------------

#ifdef MICROJXL_IMPLEMENTATION

// out(x, y) = in(x, y) * mult (after type conversion)
MICROJXL_STATIC void microjxl__(dequant_lf,P)(const microjxl__plane *in, float mult, microjxl__plane *out) {
	int32_t x, y;
	MICROJXL__ASSERT(in->type == MICROJXL__(PLANE_I,P) && out->type == MICROJXL__PLANE_F32);
	MICROJXL__ASSERT(in->width <= out->width && in->height <= out->height);
	for (y = 0; y < in->height; ++y) {
		microjxl__intP *inpixels = MICROJXL__PIXELS(in, y);
		float *outpixels = MICROJXL__F32_PIXELS(out, y);
		for (x = 0; x < in->width; ++x) outpixels[x] = (float) inpixels[x] * mult;
	}
}

// plane(x, y) += # of lf_thr[i] s.t. in(x >> hshift, y >> vshift) > lf_thr[i]
// (shifts let a subsampled LfQuant channel be evaluated into the full-resolution
// lfindices grid, replicating the same bucket across the covered region, exactly
// like libjxl's DequantDC quant_dc fill for non-444 chroma subsampling)
MICROJXL_STATIC void microjxl__(add_thresholds,P)(
	microjxl__plane *plane, const microjxl__plane *in, const int32_t *lf_thr, int32_t nb_lf_thr,
	int32_t hshift, int32_t vshift
) {
	int32_t x, y, i;
	MICROJXL__ASSERT(in->type == MICROJXL__(PLANE_I,P) && plane->type == MICROJXL__PLANE_U8);
	MICROJXL__ASSERT(in->width <= plane->width && in->height <= plane->height);
	for (y = 0; y < plane->height; ++y) {
		microjxl__intP *inpixels = MICROJXL__PIXELS(in, y >> vshift);
		uint8_t *pixels = MICROJXL__U8_PIXELS(plane, y);
		for (i = 0; i < nb_lf_thr; ++i) {
			int32_t threshold = lf_thr[i];
			for (x = 0; x < plane->width; ++x) {
				pixels[x] = (uint8_t) (pixels[x] + (inpixels[x >> hshift] > threshold));
			}
		}
	}
}

#endif // defined MICROJXL_IMPLEMENTATION

// ----------------------------------------
// end of recursion
	#undef microjxl__intP
	#undef MICROJXL__PIXELS
	#undef MICROJXL__P
#endif // MICROJXL__RECURSING == 400
#if MICROJXL__RECURSING < 0
// ----------------------------------------

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_ALWAYS_INLINE void microjxl__dequant_lf(const microjxl__plane *in, float mult, microjxl__plane *out) {
	switch (in->type) {
	case MICROJXL__PLANE_I16: microjxl__dequant_lf16(in, mult, out); break;
	case MICROJXL__PLANE_I32: microjxl__dequant_lf32(in, mult, out); break;
	default: MICROJXL__UNREACHABLE();
	}
}

MICROJXL_ALWAYS_INLINE void microjxl__add_thresholds(
	microjxl__plane *plane, const microjxl__plane *in, const int32_t *lf_thr, int32_t nb_lf_thr,
	int32_t hshift, int32_t vshift
) {
	switch (in->type) {
	case MICROJXL__PLANE_I16: microjxl__add_thresholds16(plane, in, lf_thr, nb_lf_thr, hshift, vshift); break;
	case MICROJXL__PLANE_I32: microjxl__add_thresholds32(plane, in, lf_thr, nb_lf_thr, hshift, vshift); break;
	default: MICROJXL__UNREACHABLE();
	}
}

MICROJXL_STATIC void microjxl__multiply_each_u8(microjxl__plane *plane, int32_t mult) {
	int32_t x, y;
	MICROJXL__ASSERT(plane->type == MICROJXL__PLANE_U8);
	for (y = 0; y < plane->height; ++y) {
		uint8_t *pixels = MICROJXL__U8_PIXELS(plane, y);
		for (x = 0; x < plane->width; ++x) pixels[x] = (uint8_t) (pixels[x] * mult);
	}
}

/* Reseed a group's LLF coefficients from the (frame-smoothed) LfQuant
 * planes. The 8x8-block LLF seed loop of microjxl__hf_metadata extracted
 * LLF seeds from the per-group LfQuant before smoothing; libjxl derives
 * its LLF (dc_rows) from the frame-smoothed DC image, so after the
 * frame-wide smoothing pass the group's LLF coefficients are rebuilt from
 * the smoothed values (the seed loop is simply run again here, over the
 * group's block grid, reading the stashed planes by absolute group
 * offset). */
MICROJXL_STATIC void microjxl__llf_from_lfquant(
	microjxl__st *st, const microjxl__lf_group_st *gg, const microjxl__plane lfquant[3]
) {
	microjxl__frame_st *f = st->frame;
	int32_t ggw8 = gg->width8, ggh8 = gg->height8;
	int32_t x0, y0, c;

	/* the blocks grid is fully placed at this point: cells carrying
	 * (dctsel+2)<<20 | voff are the varblock top-left corners, the same
	 * set (in the same raster order) hf_metadata's seed loop processed */
	for (y0 = 0; y0 < ggh8; ++y0) for (x0 = 0; x0 < ggw8; ++x0) {
		int32_t voff_i = MICROJXL__I32_PIXELS(&gg->blocks, y0)[x0], dctsel;
		const microjxl__dct_select *dct;
		int32_t vh8, vw8, i, j;
		int32_t coeffoff;
		if (!voff_i) continue; // not part of any varblock
		dctsel = voff_i >> 20;
		if (dctsel < 2) continue; // not a top-left block
		dct = &MICROJXL__DCT_SELECT[dctsel - 2];
		coeffoff = gg->varblocks[voff_i & 0xfffff].coeffoff_qfidx & ~15;
		if (dct->log_columns <= 3 && dct->log_rows <= 3) {
			for (c = 0; c < 3; ++c) gg->llfcoeffs[c][coeffoff >> 6] =
				MICROJXL__F32_PIXELS(&lfquant[c], y0 >> f->jpeg_vshift[c])[x0 >> f->jpeg_hshift[c]];
		} else {
			float scratch[1024]; // DCT256x256 requires 32x32
			vh8 = 1 << (dct->log_rows - 3);
			vw8 = 1 << (dct->log_columns - 3);
			for (c = 0; c < 3; ++c) {
				float *llfcoeffs_c = gg->llfcoeffs[c] + (coeffoff >> 6);
				for (i = 0; i < vh8; ++i) {
					const float *lfquantrow = MICROJXL__F32_PIXELS(&lfquant[c], (y0 + i) >> f->jpeg_vshift[c]);
					for (j = 0; j < vw8; ++j) llfcoeffs_c[i * vw8 + j] = lfquantrow[(x0 + j) >> f->jpeg_hshift[c]];
				}
				microjxl__forward_dct2d_scaled_for_llf(llfcoeffs_c, scratch, dct->log_rows - 3, dct->log_columns - 3);
			}
		}
	}
}

/* JPEG reconstruction DC capture: keeps the RAW LfQuant integers for the
 * whole frame (frame-wide grid, filled group by group). libjxl decodes to
 * JPEG with quantizer.ClearDCMul() (dec_frame.cc DecodeGlobalDCInfo), so
 * the JPEG DC is read straight from the dequantized DC plane, which then
 * equals raw_integer * 2^-extra_prec (the DC-correlation terms are 0 on
 * the jpeg-compatible streams we support). Keeping the raw integers lets
 * microjxl__jpeg_reconstruct form the exact JPEG DC = round(raw * 2^-prec)
 * - dcoff, matching libjxl's Clamp1(dc_rows[c][sbx] - dcoff, -2047, 2047)
 * with no float roundtrip. */
/* JPEG reconstruction: capture the DC plane exactly as libjxl's jpeg path
 * sees it. For jbrd streams libjxl calls Quantizer::ClearDCMul() ("Don't
 * dequant DC"), so DequantDC reduces to dc_rows[c] = raw[c] * 2^-ep (the
 * cmap's DC factors are 0 for a JPEG-compatible map). The raw integer grid
 * is therefore the exact value domain; the 2^-ep scale and the float
 * truncation happen in microjxl__jpeg_reconstruct. */
MICROJXL_STATIC void microjxl__jpeg_recon_capture_dcv(
	microjxl__st *st, const microjxl__modular *m, int32_t extra_prec,
	const microjxl__lf_group_st *gg
) {
	microjxl__frame_st *f = st->frame;
	static const int32_t YXB2XYB[3] = {1, 0, 2};
	int32_t c;
	if (!f->jpeg_recon) return;
	f->jpeg_dc_prec_shift = extra_prec;
	for (c = 0; c < 3; ++c) {
		const microjxl__plane *src = &m->channel[YXB2XYB[c]];
		int32_t hshift = f->jpeg_hshift[c], vshift = f->jpeg_vshift[c];
		int32_t fw = microjxl__ceil_div32(microjxl__ceil_div32(f->width, 8), 1 << hshift);
		int32_t fh = microjxl__ceil_div32(microjxl__ceil_div32(f->height, 8), 1 << vshift);
		int32_t gox = (gg->left >> 3) >> hshift, goy = (gg->top >> 3) >> vshift;
		int32_t y, x;
		if (src->width > fw - gox || src->height > fh - goy) {
			/* group extends past the computed frame grid: inconsistent */
			f->jpeg_recon = 0;
			return;
		}
		if (!f->jpeg_dcv[c]) {
			f->jpeg_dcv[c] = (int32_t *) MICROJXL_MALLOC((size_t) fw * fh * sizeof(int32_t));
			if (!f->jpeg_dcv[c]) { f->jpeg_recon = 0; return; }
			memset(f->jpeg_dcv[c], 0, (size_t) fw * fh * sizeof(int32_t));
		}
		f->jpeg_dcv_w[c] = fw;
		f->jpeg_dcv_h[c] = fh;
#ifdef MICROJXL_DEBUG_JPEG_TRACE
		fprintf(stderr, "[jbrd-dcv] c=%d group (%d,%d) wh=%dx%d first16:", c, gg->left, gg->top, src->width, src->height);
		{
			int32_t t;
			for (t = 0; t < 16 && t < src->width * src->height; ++t) {
				int32_t v = src->type == MICROJXL__PLANE_I16 ?
					(int32_t) MICROJXL__I16_PIXELS(src, t / src->width)[t % src->width] :
					MICROJXL__I32_PIXELS(src, t / src->width)[t % src->width];
				fprintf(stderr, " %d", v);
			}
		}
		fprintf(stderr, "\n");
#endif
		for (y = 0; y < src->height; ++y) {
			int32_t *dst = f->jpeg_dcv[c] + (size_t) (goy + y) * fw + gox;
			if (src->type == MICROJXL__PLANE_I16) {
				const int16_t *row = MICROJXL__I16_PIXELS(src, y);
				for (x = 0; x < src->width; ++x) dst[x] = row[x];
			} else {
				const int32_t *row = MICROJXL__I32_PIXELS(src, y);
				for (x = 0; x < src->width; ++x) dst[x] = row[x];
			}
		}
	}
}

/* JPEG reconstruction: keep the RAW quant table integers (libjxl
 * dec_group.cc requires qe[0].mode == kQuantModeRAW with qtable_den ==
 * 1/(8*255); the table then IS the JPEG quant table up to a transpose).
 * microjxl__read_dq_matrix folds the integers into float weights
 * 1/(qt*den) and discards them, so the capture hooks the RAW branch
 * directly. Called from hf_global for every RAW table; the first 8x8 one
 * (param 0 == DCT8X8) fills f->jpeg_qtable[c*64 + y*8 + x].
 * The double transpose (dq-matrix params[y*8+x] <- pixels[x] at read time,
 * gg->coeffs[i] <- params[i] at capture time, scaled_qtable[i] <- qtable[i]
 * here) cancels out, so qtable[] is in the same orientation as the
 * captured coefficient blocks and the writer's values[] layout. */
MICROJXL_STATIC void microjxl__jpeg_recon_capture_qtable(
	microjxl__st *st, int32_t rows, int32_t columns, const microjxl__modular *m
) {
	microjxl__frame_st *f = st->frame;
	int32_t c, y, x;
	if (!f->jpeg_recon || f->jpeg_qtable_ok) return;
	if (rows != 8 || columns != 8) { f->jpeg_recon = 0; return; }
	for (c = 0; c < 3; ++c) {
		const microjxl__plane *ch = &m->channel[c];
		for (y = 0; y < 8; ++y) {
			if (ch->type == MICROJXL__PLANE_I16) {
				const int16_t *row = MICROJXL__I16_PIXELS(ch, y);
				for (x = 0; x < 8; ++x) {
					int32_t q = row[x];
					if (q <= 0 || q >= 65536) { f->jpeg_recon = 0; return; }
					f->jpeg_qtable[c * 64 + y * 8 + x] = q;
				}
			} else {
				const int32_t *row = MICROJXL__I32_PIXELS(ch, y);
				for (x = 0; x < 8; ++x) {
					int32_t q = row[x];
					if (q <= 0 || q >= 65536) { f->jpeg_recon = 0; return; }
					f->jpeg_qtable[c * 64 + y * 8 + x] = q;
				}
			}
		}
	}
	f->jpeg_qtable_ok = 1;
#ifdef MICROJXL_DEBUG_JPEG_TRACE
	fprintf(stderr, "[jbrd] qtable[0..8]:");
	for (c = 0; c < 9; ++c) fprintf(stderr, " %d", f->jpeg_qtable[64 + c]);
	fprintf(stderr, "\n");
#endif
}

MICROJXL__STATIC_RETURNS_ERR microjxl__smooth_lf_frame(microjxl__st *st, microjxl__lf_group_st *ggs);

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_quant(
	microjxl__st *st, int32_t extra_prec, microjxl__modular *m, microjxl__lf_group_st *gg, microjxl__plane outlfquant[3]
) {
	static const int32_t YXB2XYB[3] = {1, 0, 2}; // TODO spec bug: this reordering is missing

	microjxl__frame_st *f = st->frame;
	int32_t ggw8 = gg->width8, ggh8 = gg->height8;
	microjxl__plane *channel[3], lfquant[3] = {MICROJXL__INIT}, lfindices = MICROJXL__INIT;
	int32_t c;

	// channels are subsampled according to jpeg_upsampling (YXB order in the
	// bitstream, mapped back to XYB order here), so the output planes are
	// sized per channel; lfindices stays at full (ggw8 x ggh8) resolution
	// because it is indexed by the full-resolution varblock grid
	for (c = 0; c < 3; ++c) {
		int32_t cc = YXB2XYB[c];
		microjxl__plane *ch = &m->channel[c];
		MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32,
			microjxl__ceil_div32(ggw8, 1 << f->jpeg_hshift[cc]),
			microjxl__ceil_div32(ggh8, 1 << f->jpeg_vshift[cc]), 0, &lfquant[cc]));
		(void) ch;
	}
	MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_U8, ggw8, ggh8, MICROJXL__PLANE_CLEAR, &lfindices));

	// extract LfQuant from m and populate lfindices (channel[c] is the modular
	// channel in XYB order; the YXB-order bitstream is mapped by YXB2XYB)
	/* JPEG reconstruction: keep the raw integers before dequantization
	 * (libjxl's jbrd DC path reads ClearDCMul-dequantized DC, which is the
	 * raw integer grid scaled by 2^-extra_prec; see the capture helper). */
	microjxl__jpeg_recon_capture_dcv(st, m, extra_prec, gg);

	for (c = 0; c < 3; ++c) {
		// TODO spec bug: missing 2^16 scaling
		float mult_lf = f->m_lf_scaled[c] / (float) (f->global_scale * f->quant_lf) * (float) (65536 >> extra_prec);
		channel[c] = &m->channel[YXB2XYB[c]];
		microjxl__dequant_lf(channel[c], mult_lf, &lfquant[c]);
	}
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_ORACLE")) {
		for (c = 0; c < 3; ++c) {
			int32_t j;
			fprintf(stderr, "[mj-thr] c=%d nb=%d:", c, f->nb_lf_thr[c]);
			for (j = 0; j < f->nb_lf_thr[c]; ++j) fprintf(stderr, " %d", f->lf_thr[c][j]);
			fprintf(stderr, "\n");
		}
		for (int32_t yy = 0; yy < 4 && yy < ggh8; ++yy) {
			fprintf(stderr, "[mj-raw] y=%d:", yy);
			for (int32_t xx = 0; xx < 6 && xx < ggw8; ++xx) {
				int32_t vx = channel[1]->type == MICROJXL__PLANE_I16 ? (int32_t) MICROJXL__I16_PIXELS(channel[1], yy)[xx] : MICROJXL__I32_PIXELS(channel[1], yy)[xx];
				int32_t vy = channel[0]->type == MICROJXL__PLANE_I16 ? (int32_t) MICROJXL__I16_PIXELS(channel[0], yy)[xx] : MICROJXL__I32_PIXELS(channel[0], yy)[xx];
				int32_t vb = channel[2]->type == MICROJXL__PLANE_I16 ? (int32_t) MICROJXL__I16_PIXELS(channel[2], yy)[xx] : MICROJXL__I32_PIXELS(channel[2], yy)[xx];
				fprintf(stderr, " (%d,%d,%d)", vx, vy, vb);
			}
			fprintf(stderr, "\n");
		}
	}
#endif
	// positional composition must match libjxl's DequantDC: after adding a
	// bucket term, multiply by the range of the NEXT term (X is most
	// significant, Y least): ((bx*(nbB+1) + bb) * (nbY+1) + by
	microjxl__add_thresholds(&lfindices, channel[0], f->lf_thr[0], f->nb_lf_thr[0], f->jpeg_hshift[0], f->jpeg_vshift[0]);
	microjxl__multiply_each_u8(&lfindices, f->nb_lf_thr[2] + 1);
	microjxl__add_thresholds(&lfindices, channel[2], f->lf_thr[2], f->nb_lf_thr[2], f->jpeg_hshift[2], f->jpeg_vshift[2]);
	microjxl__multiply_each_u8(&lfindices, f->nb_lf_thr[1] + 1);
	microjxl__add_thresholds(&lfindices, channel[1], f->lf_thr[1], f->nb_lf_thr[1], f->jpeg_hshift[1], f->jpeg_vshift[1]);

	/* libjxl's DequantDC bakes the LF color correlation into the DC plane
	 * (dc_x = qx*fac_x + qy*cfl_fac_x) BEFORE AdaptiveDCSmoothing runs, so
	 * the smoothing's nonlinear gap is computed on the CfL-corrected X/B
	 * channels. Mirror that here: apply the LF correlation to lfquant first,
	 * and let the combine use the corrected llf coefficients directly (it
	 * still applies the per-tile HF correlation separately). Like libjxl's
	 * DequantDC (444 branch), the correlation is only applied at full
	 * resolution; the planes are all gg->width8 x gg->height8 here. */
	if (f->jpeg_hshift[0] == 0 && f->jpeg_vshift[0] == 0 &&
		f->jpeg_hshift[1] == 0 && f->jpeg_vshift[1] == 0 &&
		f->jpeg_hshift[2] == 0 && f->jpeg_vshift[2] == 0) {
		float kx_lf = f->base_corr_x + (float) f->x_factor_lf * f->inv_colour_factor;
		float kb_lf = f->base_corr_b + (float) f->b_factor_lf * f->inv_colour_factor;
		if (kx_lf != 0.0f || kb_lf != 0.0f) {
			int32_t lf_y, lf_x;
			for (lf_y = 0; lf_y < gg->height8; ++lf_y) {
				float *row_x = MICROJXL__F32_PIXELS(&lfquant[0], lf_y);
				float *row_y = MICROJXL__F32_PIXELS(&lfquant[1], lf_y);
				float *row_b = MICROJXL__F32_PIXELS(&lfquant[2], lf_y);
				for (lf_x = 0; lf_x < gg->width8; ++lf_x) {
					if (kx_lf != 0.0f) row_x[lf_x] += kx_lf * row_y[lf_x];
					if (kb_lf != 0.0f) row_b[lf_x] += kb_lf * row_y[lf_x];
				}
			}
		}
	}

	// Adaptive DC smoothing is deferred to frame level: the post-CfL planes
	// are returned to lf_group, stashed there (after hf_metadata consumed
	// them), and smoothed on the assembled whole-frame DC grid in
	// microjxl__smooth_lf_frame, called from combine_vardct. libjxl runs
	// AdaptiveDCSmoothing on the frame-wide DC image (dec_frame.cc
	// FinalizeDC, "*must* happen between all the ProcessDCGroup and
	// ProcessACGroup"), not per LF group; smoothing per group replicates
	// group borders and diverges on multi-LF-group frames exactly at the
	// group boundaries.

#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_LF")) {
		for (c = 0; c < 3; ++c) {
			fprintf(stderr, "[jlf] c=%d (0,0)=%g (1,0)=%g (0,1)=%g\n", c,
				(double) MICROJXL__F32_PIXELS(&lfquant[c], 0)[0],
				(double) MICROJXL__F32_PIXELS(&lfquant[c], 0)[1],
				(double) MICROJXL__F32_PIXELS(&lfquant[c], 1)[0]);
		}
	}
#endif

	memcpy(outlfquant, lfquant, sizeof(microjxl__plane) * 3);
	gg->lfindices = lfindices;
	return 0;

MICROJXL__ON_ERROR:
	for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&lfquant[c]);
	microjxl__mem_free_plane(&lfindices);
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__hf_metadata(
	microjxl__st *st, int32_t nb_varblocks,
	microjxl__modular *m, const microjxl__plane lfquant[3], microjxl__lf_group_st *gg
) {
	microjxl__frame_st *f = st->frame;
	microjxl__plane blocks = MICROJXL__INIT;
	microjxl__varblock *varblocks = NULL;
	float *coeffs[3 /*xyb*/] = {NULL}, *llfcoeffs[3 /*xyb*/] = {NULL};
	size_t coeffs_misalign[3] = {0};
	int32_t log_gsize8 = f->group_size_shift - 3;
	int32_t ggw8 = gg->width8, ggh8 = gg->height8;
	int32_t voff, coeffoff;
	int32_t x0, y0, x1, y1, i, j, c;

	gg->xfromy = m->channel[0];
	gg->bfromy = m->channel[1];
	gg->sharpness = m->channel[3];
	memset(&m->channel[0], 0, sizeof(microjxl__plane));
	memset(&m->channel[1], 0, sizeof(microjxl__plane));
	memset(&m->channel[3], 0, sizeof(microjxl__plane));

	MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_I32, ggw8, ggh8, MICROJXL__PLANE_CLEAR, &blocks));
	MICROJXL__TRY_MALLOC(microjxl__varblock, &varblocks, (size_t) nb_varblocks);
	for (c = 0; c < 3; ++c) { // TODO account for chroma subsampling
		MICROJXL__TRY_MALLOC(float, &llfcoeffs[c], (size_t) (ggw8 * ggh8));
		MICROJXL__SHOULD(
			coeffs[c] = (float*) microjxl__alloc_aligned(
				sizeof(float) * (size_t) (ggw8 * ggh8 * 64), MICROJXL__COEFFS_ALIGN, &coeffs_misalign[c]),
			"!mem");
		for (i = 0; i < ggw8 * ggh8 * 64; ++i) coeffs[c][i] = 0.0f;
	}

	// temporarily use coeffoff_qfidx to store DctSelect
	if (m->channel[2].type == MICROJXL__PLANE_I16) {
		int16_t *blockinfo0 = MICROJXL__I16_PIXELS(&m->channel[2], 0);
		int16_t *blockinfo1 = MICROJXL__I16_PIXELS(&m->channel[2], 1);
		for (i = 0; i < nb_varblocks; ++i) {
			varblocks[i].coeffoff_qfidx = blockinfo0[i];
			varblocks[i].hfmul.m1 = blockinfo1[i];
			varblocks[i].row_quant = blockinfo1[i] + 1;
		}
	} else {
		int32_t *blockinfo0 = MICROJXL__I32_PIXELS(&m->channel[2], 0);
		int32_t *blockinfo1 = MICROJXL__I32_PIXELS(&m->channel[2], 1);
		for (i = 0; i < nb_varblocks; ++i) {
			varblocks[i].coeffoff_qfidx = blockinfo0[i];
			varblocks[i].hfmul.m1 = blockinfo1[i];
			varblocks[i].row_quant = blockinfo1[i] + 1;
		}
	}

	// place varblocks
	voff = coeffoff = 0;
	for (y0 = 0; y0 < ggh8; ++y0) for (x0 = 0; x0 < ggw8; ++x0) {
		int32_t dctsel, log_vh, log_vw, vh8, vw8;
		const microjxl__dct_select *dct;
		if (MICROJXL__I32_PIXELS(&blocks, y0)[x0]) continue;

#ifdef MICROJXL_DEBUG
#endif

		MICROJXL__SHOULD(voff < nb_varblocks, "vblk"); // TODO spec issue: missing
		dctsel = varblocks[voff].coeffoff_qfidx;
		MICROJXL__SHOULD(0 <= dctsel && dctsel < MICROJXL__NUM_DCT_SELECT, "dct?");
		dct = &MICROJXL__DCT_SELECT[dctsel];
		f->dct_select_used |= 1 << dctsel;
		f->order_used |= 1 << dct->order_idx;
		varblocks[voff].coeffoff_qfidx = coeffoff;
		MICROJXL__ASSERT(coeffoff % 64 == 0);

		log_vh = dct->log_rows;
		log_vw = dct->log_columns;
		MICROJXL__ASSERT(log_vh >= 3 && log_vw >= 3 && log_vh <= 8 && log_vw <= 8);
		vw8 = 1 << (log_vw - 3);
		vh8 = 1 << (log_vh - 3);
		x1 = x0 + vw8 - 1;
		y1 = y0 + vh8 - 1;
		// SPEC the first available block in raster order SHOULD be the top-left corner of
		// the next varblock, otherwise it's an error (no retry required)
		MICROJXL__SHOULD(x1 < ggw8 && (x0 >> log_gsize8) == (x1 >> log_gsize8), "vblk");
		MICROJXL__SHOULD(y1 < ggh8 && (y0 >> log_gsize8) == (y1 >> log_gsize8), "vblk");

		for (i = 0; i < vh8; ++i) {
			int32_t *blockrow = MICROJXL__I32_PIXELS(&blocks, y0 + i);
			for (j = 0; j < vw8; ++j) blockrow[x0 + j] = 1 << 20 | voff;
		}
		MICROJXL__I32_PIXELS(&blocks, y0)[x0] = (dctsel + 2) << 20 | voff;

		// compute LLF coefficients from dequantized LF; subsampled channels are
		// read at shifted coordinates so the same DC value is replicated across
		// the covered 2x2 (or 4x4) block region, matching libjxl's dc_rows[sbx]
		if (log_vw <= 3 && log_vh <= 3) {
			for (c = 0; c < 3; ++c) llfcoeffs[c][coeffoff >> 6] =
				MICROJXL__F32_PIXELS(&lfquant[c], y0 >> f->jpeg_vshift[c])[x0 >> f->jpeg_hshift[c]];
		} else {
			float scratch[1024]; // DCT256x256 requires 32x32
			for (c = 0; c < 3; ++c) {
				float *llfcoeffs_c = llfcoeffs[c] + (coeffoff >> 6);
				for (i = 0; i < vh8; ++i) {
					float *lfquantrow = MICROJXL__F32_PIXELS(&lfquant[c], (y0 + i) >> f->jpeg_vshift[c]);
					for (j = 0; j < vw8; ++j) llfcoeffs_c[i * vw8 + j] = lfquantrow[(x0 + j) >> f->jpeg_hshift[c]];
				}
				// TODO spec bug: DctSelect type IDENTIFY [sic] no longer exists
				// TODO spec issue: DCT8x8 doesn't need this
				microjxl__forward_dct2d_scaled_for_llf(llfcoeffs_c, scratch, log_vh - 3, log_vw - 3);
			}
		}

		coeffoff += 1 << (log_vw + log_vh);
		++voff;
	}
	MICROJXL__SHOULD(voff == nb_varblocks, "vblk"); // TODO spec issue: missing
	// TODO both libjxl and spec don't check for coeffoff == ggw8 * ggh8, but they probably should?

	// compute qf_idx and hfmul.inv for later use
	MICROJXL__ASSERT(f->nb_qf_thr < 16);
	for (j = 0; j < f->nb_qf_thr; ++j) {
		for (i = 0; i < nb_varblocks; ++i) {
			varblocks[i].coeffoff_qfidx += varblocks[i].hfmul.m1 >= f->qf_thr[j];
		}
	}
	for (i = 0; i < nb_varblocks; ++i) {
		varblocks[i].hfmul.inv = 1.0f / ((float) varblocks[i].hfmul.m1 + 1.0f);
	}

	gg->nb_varblocks = nb_varblocks;
	gg->blocks = blocks;
	gg->varblocks = varblocks;
	for (c = 0; c < 3; ++c) {
		gg->llfcoeffs[c] = llfcoeffs[c];
		gg->coeffs[c] = coeffs[c];
		gg->coeffs_misalign[c] = (uint8_t) coeffs_misalign[c];
	}
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_plane(&blocks);
	microjxl__mem_free(varblocks);
	for (c = 0; c < 3; ++c) {
		microjxl__mem_free_aligned(coeffs[c], MICROJXL__COEFFS_ALIGN, coeffs_misalign[c]);
		microjxl__mem_free(llfcoeffs[c]);
	}
	return st->err;
}

/* LF frame (spec LFFrame, frame_type == kLFFrame; libjxl "DC frame",
 * FrameType::kDCFrame): a self-contained VarDCT frame whose samples ARE
 * the LF of a future frame. It decodes through the full regular pipeline
 * (LF + HF of its own bitstream) and is captured BEFORE the colour
 * transform (libjxl dec_frame.cc: kDCFrame forces save_before_ct; the
 * render pipeline writes shared->dc via a WriteToImage3FStage placed
 * after patches/splines/noise and before the XYB stage, dec_cache.cc
 * GetWriteToImage3FStage(&dc_frames[dc_level - 1])). Storage domain:
 * correlated (px, py, pb) XYB floats, i.e. exactly the ref_snap carrier
 * the combine already fills for save_before_ct frames.
 * Storage index: libjxl stores at dc_frames[dc_level - 1] and a
 * kUseDcFrame consumer with header dc_level D reads dc_frames[D]
 * (passes_state.cc), so a frame with lf_level L is stored at
 * lf_frames[L - 1] and read by a consumer with lf_level D as
 * lf_frames[D] (i.e. the frame whose header lf_level was D+1). */
MICROJXL__STATIC_RETURNS_ERR microjxl__decode_lf_frame(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t dst = f->lf_level - 1;
	int32_t c, x, y;
	microjxl__plane out[3] = {MICROJXL__INIT, MICROJXL__INIT, MICROJXL__INIT};

	/* decode_lf_frame runs as a whole in one yield step (called via a
	 * single MICROJXL__YIELD_AFTER in the advance loop, after the frame
	 * has fully decoded), so local state needs no coroutine re-entry
	 * handling. */
	if (f->lf_level < 1 || f->lf_level > 4) MICROJXL__RAISE("flvl");
	if (f->ref_snap[0].type == MICROJXL__PLANE_F32 && !f->do_ycbcr) {
		/* VarDCT: the combine filled ref_snap with the post-noise
		 * correlated XYB floats at the save-before-colour-transform point
		 * — exactly libjxl's DC-frame capture (WriteToImage3FStage). */
		for (c = 0; c < 3; ++c) {
			out[c] = f->ref_snap[c];
			memset(&f->ref_snap[c], 0, sizeof(f->ref_snap[c]));
		}
	} else if (f->is_modular) {
		/* Modular DC frames (cjxl encodes them as modular): derive the
		 * correlated XYB floats from gmodular exactly like the render's
		 * XYB branch (X = ch1*m_lf_scaled[0], Y = ch0*m_lf_scaled[1],
		 * B = (ch2+ch0)*m_lf_scaled[2]). Patches already blended into
		 * gmodular by apply_patches_modular; DC frames carry no splines
		 * or noise (cjxl), so this is the same capture point. */
		int32_t color_channels = (f->gmodular.num_channels >= 3) ? 3 : 1;
		int32_t maxpixel = microjxl__maxpixel_scale(im);
		for (c = 0; c < 3; ++c) {
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32,
				f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &out[c]));
		}
		for (y = 0; y < f->height; ++y) {
			for (x = 0; x < f->width; ++x) {
				if (im->xyb_encoded && color_channels == 3) {
					int32_t chv[3], ci;
					for (ci = 0; ci < 3; ++ci) {
						microjxl__plane *cp = &f->gmodular.channel[ci];
						chv[ci] = cp->type == MICROJXL__PLANE_I32 ?
							MICROJXL__I32_PIXELS(cp, y)[x] :
							(cp->type == MICROJXL__PLANE_I16 ? MICROJXL__I16_PIXELS(cp, y)[x] : 0);
					}
					MICROJXL__F32_PIXELS(&out[0], y)[x] = (float) chv[1] * f->m_lf_scaled[0];
					MICROJXL__F32_PIXELS(&out[1], y)[x] = (float) chv[0] * f->m_lf_scaled[1];
					MICROJXL__F32_PIXELS(&out[2], y)[x] = (float) ((int64_t) chv[2] + chv[0]) * f->m_lf_scaled[2];
				} else {
					/* non-XYB DC frames: display-domain floats in [0,1]
					 * (gray replicates the luma channel). The consumer must
					 * use the same encoding for the samples to line up. */
					int32_t ci;
					for (ci = 0; ci < 3; ++ci) {
						microjxl__plane *cp = &f->gmodular.channel[ci < color_channels ? ci : 0];
						int32_t v = cp->type == MICROJXL__PLANE_I32 ?
							MICROJXL__I32_PIXELS(cp, y)[x] :
							(cp->type == MICROJXL__PLANE_I16 ? MICROJXL__I16_PIXELS(cp, y)[x] : 0);
						MICROJXL__F32_PIXELS(&out[ci], y)[x] = (float) v / (float) maxpixel;
					}
				}
			}
		}
	} else {
		/* VarDCT YCbCr (or a genuinely empty capture): libjxl only writes
		 * dc_frames for XYB/linear pipelines it supports; a YCbCr DC frame
		 * has no XYB-domain samples to store. Reject rather than silently
		 * storing zeros. */
		MICROJXL__RAISE("lfsn: no XYB-domain samples captured for this DC frame");
	}
	for (c = 0; c < 3; ++c) {
		microjxl__mem_free_plane(&im->lf_frames[dst][c]);
		im->lf_frames[dst][c] = out[c];
	}
	im->lf_frame_present[dst] = 1;
	microjxl__mem_free_frame_state(f);
	return 0; // ownership of out[] moved into im->lf_frames; do NOT fall through

MICROJXL__ON_ERROR:
	for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&out[c]);
	return st->err;
}

/* Frame-wide adaptive DC smoothing (libjxl dec_frame.cc FinalizeDC ->
 * compressed_dc.cc AdaptiveDCSmoothing). lf_quant stashes each LF group's
 * dequantized post-CfL LfQuant planes in f->lf_stash instead of running
 * microjxl__smooth_lf per group; this function assembles them on the
 * whole-frame block grid (gg.left/top >> 3), smooths the full image with
 * neighbours taken across group seams, blits the result back to the group
 * planes, and rewrites every group's LLF coefficients — which are derived
 * from the (now frame-smoothed) DC values in hf_metadata — via
 * microjxl__llf_from_lfquant.
 * The groups' hf_metadata has already run by the time this is called (LF
 * groups decode before hf_global / AC groups), so the LfQuant-derived
 * lf_idx thresholds in gg->lfindices were computed from the pre-smoothing
 * values; libjxl has the same property (block ctx map built per DC group
 * from unsmoothed LfQuant, smoothing in FinalizeDC afterwards). */
MICROJXL__STATIC_RETURNS_ERR microjxl__smooth_lf_frame(microjxl__st *st, microjxl__lf_group_st *ggs) {
	microjxl__frame_st *f = st->frame;
	microjxl__plane full[3] = {MICROJXL__INIT, MICROJXL__INIT, MICROJXL__INIT};
	microjxl__plane sm[3] = {MICROJXL__INIT, MICROJXL__INIT, MICROJXL__INIT};
	int32_t c, i, y, x, bW, bH;

	if (!f->lf_stash_have || f->is_modular) return 0;
	bW = microjxl__ceil_div32(f->width, 8);
	bH = microjxl__ceil_div32(f->height, 8);
	for (c = 0; c < 3; ++c) {
		/* full = assembled DC grid (kernel input), sm = smoothed result:
		 * the 3x3 kernel reads full rows y-1/y/y+1 while writing sm, like
		 * libjxl's AdaptiveDCSmoothing (smoothing into a copy) */
		MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, bW, bH,
			MICROJXL__PLANE_FORCE_PAD, &full[c]));
		MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, bW, bH,
			MICROJXL__PLANE_CLEAR, &sm[c]));
		for (y = 0; y < bH; ++y) {
			float *row = MICROJXL__F32_PIXELS(&full[c], y);
			for (x = 0; x < bW; ++x) row[x] = 0.0f;
		}
	}
	for (i = 0; i < (int32_t) f->num_lf_groups; ++i) {
		microjxl__lf_group_st *gg = &ggs[i];
		int32_t bx = gg->left >> 3, by = gg->top >> 3;
		for (y = 0; y < gg->height8; ++y) {
			for (c = 0; c < 3; ++c) {
				const float *srow = MICROJXL__F32_PIXELS(&f->lf_stash[i * 3 + c], y);
				float *drow = MICROJXL__F32_PIXELS(&full[c], by + y);
				for (x = 0; x < gg->width8; ++x) drow[bx + x] = srow[x];
			}
		}
	}
	/* same 3x3 adaptive smoothing as microjxl__smooth_lf, on the frame
	 * grid; like libjxl's AdaptiveDCSmoothing ("Fill in borders that the
	 * loop below will not"), the border rows/columns are copied from the
	 * input unchanged */
	for (c = 0; c < 3; ++c) {
		memcpy(MICROJXL__F32_PIXELS(&sm[c], 0), MICROJXL__F32_PIXELS(&full[c], 0), sizeof(float) * (size_t) bW);
		memcpy(MICROJXL__F32_PIXELS(&sm[c], bH - 1), MICROJXL__F32_PIXELS(&full[c], bH - 1), sizeof(float) * (size_t) bW);
		for (y = 1; y < bH - 1; ++y) {
			MICROJXL__F32_PIXELS(&sm[c], y)[0] = MICROJXL__F32_PIXELS(&full[c], y)[0];
			MICROJXL__F32_PIXELS(&sm[c], y)[bW - 1] = MICROJXL__F32_PIXELS(&full[c], y)[bW - 1];
		}
	}
	{
		static const float W0 = 0.05226273532324128f, W1 = 0.20345139757231578f, W2 = 0.0334829185968739f;
		float inv_m_lf[3];
		for (c = 0; c < 3; ++c) {
			// TODO spec bug: missing 2^16 scaling (same as microjxl__smooth_lf)
			inv_m_lf[c] = (float) (f->global_scale * f->quant_lf) / f->m_lf_scaled[c] / 65536.0f;
		}
		for (y = 1; y < bH - 1; ++y) {
			const float *line[3], *nline[3], *sline[3];
			for (c = 0; c < 3; ++c) {
				nline[c] = MICROJXL__F32_PIXELS(&full[c], y - 1);
				line[c] = MICROJXL__F32_PIXELS(&full[c], y);
				sline[c] = MICROJXL__F32_PIXELS(&full[c], y + 1);
			}
			for (x = 1; x < bW - 1; ++x) {
				float wa[3], diff[3], gap = 0.5f;
				for (c = 0; c < 3; ++c) {
					wa[c] =
						(nline[c][x - 1] * W2 + nline[c][x] * W1 + nline[c][x + 1] * W2) +
						( line[c][x - 1] * W1 +  line[c][x] * W0 +  line[c][x + 1] * W1) +
						(sline[c][x - 1] * W2 + sline[c][x] * W1 + sline[c][x + 1] * W2);
					diff[c] = fabsf(wa[c] - line[c][x]) * inv_m_lf[c];
					if (gap < diff[c]) gap = diff[c];
				}
				gap = microjxl__maxf(0.0f, 3.0f - 4.0f * gap);
				// TODO spec bug: s (sample) and wa (weighted average) are swapped
				// in the final formula (same as microjxl__smooth_lf)
				for (c = 0; c < 3; ++c)
					MICROJXL__F32_PIXELS(&sm[c], y)[x] = (wa[c] - line[c][x]) * gap + line[c][x];
			}
		}
	}
	for (i = 0; i < (int32_t) f->num_lf_groups; ++i) {
		microjxl__lf_group_st *gg = &ggs[i];
		int32_t bx = gg->left >> 3, by = gg->top >> 3;
		for (y = 0; y < gg->height8; ++y) {
			for (c = 0; c < 3; ++c) {
				const float *srow = MICROJXL__F32_PIXELS(&sm[c], by + y);
				float *drow = MICROJXL__F32_PIXELS(&f->lf_stash[i * 3 + c], y);
				for (x = 0; x < gg->width8; ++x) drow[x] = srow[bx + x];
			}
		}
	}
	/* LLF coefficients live in the groups' coeffs arrays: rebuild them
	 * from the now-smoothed planes (they were seeded from the pre-smoothing
	 * values in hf_metadata). */
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_DUMP_DCSM")) {
		FILE *df = fopen(getenv("MICROJXL_DUMP_DCSM"), "wb");
		if (df) {
			for (c = 0; c < 3; ++c) for (y = 0; y < bH; ++y)
				fwrite(MICROJXL__F32_PIXELS(&sm[c], y), sizeof(float), (size_t) bW, df);
			fclose(df);
		}
	}
#endif
	for (i = 0; i < (int32_t) f->num_lf_groups; ++i) {
		microjxl__lf_group_st *gg = &ggs[i];
		const microjxl__plane lfq[3] = {f->lf_stash[i * 3 + 0], f->lf_stash[i * 3 + 1], f->lf_stash[i * 3 + 2]};
		microjxl__llf_from_lfquant(st, gg, lfq);
		for (c = 0; c < 3; ++c) {
			microjxl__mem_free_plane(&f->lf_stash[i * 3 + c]);
		}
	}
	microjxl__mem_free(f->lf_stash);
	f->lf_stash = NULL;
	f->lf_stash_have = 0;

MICROJXL__ON_ERROR:
	for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&full[c]);
	for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&sm[c]);
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_group(microjxl__st *st, microjxl__lf_group_st *gg) {
	microjxl__frame_st *f = st->frame;
	microjxl__image_st *im = st->image;
	int64_t sidx0 = 1 + gg->idx, sidx1 = 1 + f->num_lf_groups + gg->idx, sidx2 = 1 + 2 * f->num_lf_groups + gg->idx;
	microjxl__plane lfquant[3] = {{0}};
	int lfquant_owned = 1; /* planes produced by lf_quant are owned */
	microjxl__modular m = MICROJXL__INIT;
	int32_t i;

	/* G.2.2 ModularLfGroup: the 'small' modular channels (both shifts >= 3,
	 * e.g. squeeze-pyramid residuals) are decoded from this group's ModularDC
	 * stream (sidx1). Channels with a shift < 3 belong to the pass-group
	 * streams instead and are skipped here. libjxl reference:
	 * dec_frame.cc ProcessDCGroup -> ModularFrameDecoder::DecodeGroup with
	 * minShift=3, maxShift=1000 over the DC-group rect
	 * (dc_group_dim = group_dim << 3, frame_dimensions.h). The decoded
	 * samples are pasted back into the global modular channels; inverse
	 * transforms of f->gmodular run once, frame-wide, at frame finalize. */
	if (f->gmodular.num_channels > f->num_gm_channels) {
		int32_t ggdim = (1 << f->group_size_shift) << 3;
		int32_t have_small = 0;
		for (i = f->num_gm_channels; i < f->gmodular.num_channels; ++i) {
			microjxl__plane *c = &f->gmodular.channel[i];
			if (c->hshift >= 3 && c->vshift >= 3) have_small = 1;
		}
		if (have_small) {
			microjxl__modular mdc = MICROJXL__INIT;
			MICROJXL__TRY(microjxl__init_modular_for_lf_group(st, f->num_gm_channels,
				ggdim, gg->left, gg->top, &f->gmodular, &mdc));
			if (mdc.num_channels > 0) {
				MICROJXL__TRY(microjxl__modular_header(st, f->global_tree, &f->global_codespec, &mdc));
				MICROJXL__TRY(microjxl__allocate_modular(st, &mdc));
				for (i = 0; i < mdc.num_channels; ++i) {
#ifdef MICROJXL_DEBUG
					if (getenv("MICROJXL_TRACE_STREAM")) fprintf(stderr, "[mjch] lf-group-modular g=%lld ch=%d sidx=%lld\n", (long long) gg->idx, i, (long long) sidx1);
#endif
					MICROJXL__TRY(microjxl__modular_channel(st, &mdc, i, sidx1));
				}
				MICROJXL__TRY(microjxl__finish_and_free_code(st, &mdc.code));
				microjxl__combine_modular_from_lf_group(f->num_gm_channels,
					ggdim, gg->top, gg->left, &f->gmodular, &mdc);
			}
			microjxl__mem_free_modular(&mdc);
		}
	}

	if (!f->is_modular) {
		int32_t ggw8 = gg->width8, ggh8 = gg->height8;
		int32_t ggw64 = gg->width64, ggh64 = gg->height64;
		int32_t w[4], h[4], nb_varblocks;

		MICROJXL__ASSERT(ggw8 <= 1024 && ggh8 <= 1024);

		// LfQuant
		if (!f->use_lf_frame) {
			int32_t extra_prec = microjxl__u(st, 2), c;
			/* spec 18181-1 G.2.2: LfQuant consists of three channels whose
			 * dimensions are right-shifted per channel according to
			 * jpeg_upsampling (the channels are in YXB order). */
			static const int32_t YXB2XYB[3] = {1, 0, 2}; // TODO spec bug: this reordering is missing
			for (c = 0; c < 3; ++c) {
				int32_t cc = YXB2XYB[c];
				w[c] = microjxl__ceil_div32(ggw8, 1 << f->jpeg_hshift[cc]);
				h[c] = microjxl__ceil_div32(ggh8, 1 << f->jpeg_vshift[cc]);
			}
			MICROJXL__TRY(microjxl__init_modular(st, 3, w, h, &m));
			MICROJXL__TRY(microjxl__modular_header(st, f->global_tree, &f->global_codespec, &m));
			MICROJXL__TRY(microjxl__allocate_modular(st, &m));
			for (c = 0; c < m.num_channels; ++c) MICROJXL__TRY(microjxl__modular_channel(st, &m, c, sidx0));
			MICROJXL__TRY(microjxl__finish_and_free_code(st, &m.code));
			MICROJXL__TRY(microjxl__inverse_transform(st, &m));
			// TODO spec issue: this modular image is independent of bpp/float_sample/etc.
			// TODO spec bug: channels are in the YXB order
			MICROJXL__TRY(microjxl__lf_quant(st, extra_prec, &m, gg, lfquant));
			microjxl__mem_free_modular(&m);
		} else {
			/* spec G.2.2: with kUseLfFrame the LfQuant sub-bitstream is
			 * skipped and the samples of LFFrame[frame_header.lf_level] are
			 * used instead of the values this subclause (and I.5.2) would
			 * compute. libjxl implements this by pointing shared->dc at the
			 * decoded DC frame (passes_state.cc): the LLF extraction reads
			 * the DC-frame samples as per-block DC values. The DC-frame grid
			 * is ceil(frame/8) samples = exactly this frame's block grid, so
			 * lfquant[c] receives this group's block rectangle blitted from
			 * the stored full-frame grid (lfquant planes are group-local,
			 * indexed by block coords within the group). */
			int32_t D = f->lf_level; /* store slot of the consumed frame */
			int32_t c;
			MICROJXL__SHOULD(f->lf_level <= 3, "flvl");
			MICROJXL__SHOULD(im->lf_frame_present[D], "lfrm");
			for (c = 0; c < 3; ++c) {
				const microjxl__plane *src = &im->lf_frames[D][c];
				int32_t bx = gg->left >> 3, by = gg->top >> 3, yy;
				MICROJXL__SHOULD(src->type == MICROJXL__PLANE_F32, "lfrm");
				MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32,
					gg->width8, gg->height8, 0, &lfquant[c]));
				for (yy = 0; yy < gg->height8; ++yy) {
					const float *srow = MICROJXL__F32_PIXELS(src, by + yy);
					float *drow = MICROJXL__F32_PIXELS(&lfquant[c], yy);
					int32_t xx;
					for (xx = 0; xx < gg->width8; ++xx) drow[xx] = srow[bx + xx];
				}
			}
			/* the blitted planes are owned (freed below / on error) */
			lfquant_owned = 1;
			/* spec G.2.2: with kUseLfFrame the quantized LfQuant coefficients
			 * count as all-zero, so lf_idx (BlockContext) is always 0. lf_quant
			 * normally produces this plane from the decoded thresholds; with
			 * no LfQuant sub-bitstream a cleared plane yields the same all-zero
			 * lf_idx everywhere. Ownership passes to gg (freed by
			 * microjxl__mem_free_lf_group). */
			{
				microjxl__plane lfindices = {0};
				MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_U8, gg->width8, gg->height8, MICROJXL__PLANE_CLEAR, &lfindices));
				gg->lfindices = lfindices;
			}
		}

		// HF metadata
		// SPEC nb_block is off by one
		nb_varblocks = microjxl__u(st, microjxl__ceil_lg32((uint32_t) (ggw8 * ggh8))) + 1; // at most 2^20
		w[0] = w[1] = ggw64; h[0] = h[1] = ggh64; // XFromY, BFromY
		w[2] = nb_varblocks; h[2] = 2; // BlockInfo
		w[3] = ggw8; h[3] = ggh8; // Sharpness
		MICROJXL__TRY(microjxl__init_modular(st, 4, w, h, &m));
		MICROJXL__TRY(microjxl__modular_header(st, f->global_tree, &f->global_codespec, &m));
		MICROJXL__TRY(microjxl__allocate_modular(st, &m));
		for (i = 0; i < m.num_channels; ++i) MICROJXL__TRY(microjxl__modular_channel(st, &m, i, sidx2));
		MICROJXL__TRY(microjxl__finish_and_free_code(st, &m.code));
		MICROJXL__TRY(microjxl__inverse_transform(st, &m));
		MICROJXL__TRY(microjxl__hf_metadata(st, nb_varblocks, &m, lfquant, gg));
		microjxl__mem_free_modular(&m);
		/* Stash the post-CfL LfQuant planes for the frame-wide adaptive DC
		 * smoothing (microjxl__smooth_lf_frame, called from combine_vardct
		 * once every LF group is decoded): libjxl runs AdaptiveDCSmoothing
		 * on the assembled frame-wide DC image (dec_frame.cc FinalizeDC)
		 * after all DC groups, not per LF group. Ownership of the planes
		 * moves to f->lf_stash here; hf_metadata has already consumed them
		 * (LLF seed + lf_idx), matching libjxl's order (the block ctx map
		 * is built per DC group from the unsmoothed LfQuant, FinalizeDC
		 * smooths afterwards, and the LLF/dc_rows is derived from the
		 * smoothed image — which smooth_lf_frame restores by reseeding the
		 * group's LLF coefficients). Never for kUseLfFrame (libjxl's
		 * kUseDcFrame skips AdaptiveDCSmoothing) or subsampled chroma. */
		if (!f->use_lf_frame && !f->skip_adapt_lf_smooth &&
			f->jpeg_hshift[0] == 0 && f->jpeg_vshift[0] == 0 &&
			f->jpeg_hshift[1] == 0 && f->jpeg_vshift[1] == 0 &&
			f->jpeg_hshift[2] == 0 && f->jpeg_vshift[2] == 0) {
			if (!f->lf_stash) {
				MICROJXL__TRY_CALLOC(microjxl__plane, &f->lf_stash, (size_t) f->num_lf_groups * 3);
			}
			for (i = 0; i < 3; ++i) {
				f->lf_stash[gg->idx * 3 + i] = lfquant[i];
				memset(&lfquant[i], 0, sizeof(lfquant[i]));
			}
			f->lf_stash_have = 1;
		}
		if (lfquant_owned) {
			for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&lfquant[i]);
		}
	}

	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_modular(&m);
	if (lfquant_owned) {
		for (i = 0; i < 3; ++i) microjxl__mem_free_plane(&lfquant[i]);
	}
	if (gg) microjxl__mem_free_lf_group(gg);
	return st->err;
}

MICROJXL_STATIC void microjxl__mem_free_lf_group(microjxl__lf_group_st *gg) {
	int32_t i;
	for (i = 0; i < 3; ++i) {
		microjxl__mem_free(gg->llfcoeffs[i]);
		microjxl__mem_free_aligned(gg->coeffs[i], MICROJXL__COEFFS_ALIGN, gg->coeffs_misalign[i]);
		gg->llfcoeffs[i] = NULL;
		gg->coeffs[i] = NULL;
	}
	microjxl__mem_free_plane(&gg->xfromy);
	microjxl__mem_free_plane(&gg->bfromy);
	microjxl__mem_free_plane(&gg->sharpness);
	microjxl__mem_free_plane(&gg->blocks);
	microjxl__mem_free_plane(&gg->lfindices);
	microjxl__mem_free(gg->varblocks);
	gg->varblocks = NULL;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// HfGlobal and HfPass

MICROJXL__STATIC_RETURNS_ERR microjxl__hf_global(microjxl__st *st);

#ifdef MICROJXL_IMPLEMENTATION

// reads both HfGlobal and HfPass (SPEC they form a single group)
MICROJXL__STATIC_RETURNS_ERR microjxl__hf_global(microjxl__st *st) {
	microjxl__frame_st *f = st->frame;
	int64_t sidx_base = 1 + 3 * f->num_lf_groups;
	microjxl__code_spec codespec = MICROJXL__INIT;
	microjxl__code_st code = MICROJXL__INIT;
	int32_t i, j, c;

	MICROJXL__ASSERT(!f->is_modular);
#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[microjxl] hf_global start: bitpos=%lld sidx_base=%lld\n", (long long) microjxl__bits_read(st), (long long) sidx_base);
#endif

	// dequantization matrices
	if (!microjxl__u(st, 1)) {
		// TODO spec improvement: encoding mode 1..5 are only valid for 0-3/9-10 since it requires 8x8 matrix, explicitly note this
		for (i = 0; i < MICROJXL__NUM_DCT_PARAMS; ++i) { // SPEC not 11, should be 17
			const struct microjxl__dct_params dct = MICROJXL__DCT_PARAMS[i];
			int32_t rows = 1 << (int32_t) dct.log_rows, columns = 1 << (int32_t) dct.log_columns;
			MICROJXL__TRY(microjxl__read_dq_matrix(st, rows, columns, sidx_base + i,
				f->global_tree, &f->global_codespec, &f->dq_matrix[i]));
		}
	}

	// TODO is it possible that num_hf_presets > num_groups? otherwise microjxl__at_most is better
	f->num_hf_presets = microjxl__u(st, microjxl__ceil_lg32((uint32_t) f->num_groups)) + 1;
	MICROJXL__RAISE_DELAYED();

	// HfPass
	for (i = 0; i < f->num_passes; ++i) {
		int32_t used_orders = microjxl__u32(st, 0x5f, 0, 0x13, 0, 0, 0, 0, 13);
		if (used_orders > 0) {
			MICROJXL__TRY(microjxl__read_code_spec(st, 8, &codespec));
			microjxl__init_code(&code, &codespec);
		}
		for (j = 0; j < MICROJXL__NUM_ORDERS; ++j) {
			if (used_orders >> j & 1) {
				int32_t size = 1 << (MICROJXL__LOG_ORDER_SIZE[j][0] + MICROJXL__LOG_ORDER_SIZE[j][1]);
				for (c = 0; c < 3; ++c) { // SPEC this loop is omitted
					MICROJXL__TRY(microjxl__permutation(st, &code, size, size / 64, &f->orders[i][j][c]));
				}
			}
		}
		if (used_orders > 0) {
			MICROJXL__TRY(microjxl__finish_and_free_code(st, &code));
			microjxl__mem_free_code_spec(&codespec);
		}

		MICROJXL__TRY(microjxl__read_code_spec(st, 495 * f->nb_block_ctx * f->num_hf_presets, &f->coeff_codespec[i]));
		/* libjxl dec_frame.cc pads the AC context map by
		 * kZeroDensityContextLimit - kZeroDensityContextCount = 16 zero entries
		 * ("cheat in hot loop"): ZeroDensityContext can reach 473 > 458 on
		 * corrupt streams. Mirror that by extending the cluster map with
		 * entries that map to cluster 0. */
		MICROJXL__TRY(microjxl__pad_cluster_map(st, &f->coeff_codespec[i], MICROJXL__COEFF_CTX_PAD));
	}

MICROJXL__ON_ERROR:
	microjxl__mem_free_code(&code);
	microjxl__mem_free_code_spec(&codespec);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// PassGroup

MICROJXL__STATIC_RETURNS_ERR microjxl__hf_coeffs(
	microjxl__st *st, int32_t ctxoff, int32_t pass,
	int32_t gx_in_gg, int32_t gy_in_gg, int32_t gw, int32_t gh, microjxl__lf_group_st *gg
);
MICROJXL__STATIC_RETURNS_ERR microjxl__pass_group(
	microjxl__st *st, int32_t pass, int32_t gx_in_gg, int32_t gy_in_gg, int32_t gw, int32_t gh, int64_t gidx,
	microjxl__lf_group_st *gg
);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__hf_coeffs(
	microjxl__st *st, int32_t ctxoff, int32_t pass,
	int32_t gx_in_gg, int32_t gy_in_gg, int32_t gw, int32_t gh, microjxl__lf_group_st *gg
) {
	typedef int8_t microjxl_i8x3[3];
	const microjxl__frame_st *f = st->frame;
	/* libjxl dec_group.cc: shift_for_pass[pass] scales each pass's coefficient
	 * magnitude; passes accumulate into the same dequantized-coefficient
	 * buffer (passes_state.cc). */
	int32_t coef_shift = f->num_passes > 1 ? f->shift[pass] : 0;
	int32_t gw8 = microjxl__ceil_div32(gw, 8), gh8 = microjxl__ceil_div32(gh, 8);
	int8_t (*nonzeros)[3] = NULL;
	microjxl__code_st code = MICROJXL__INIT;
	int32_t lfidx_size = (f->nb_lf_thr[0] + 1) * (f->nb_lf_thr[1] + 1) * (f->nb_lf_thr[2] + 1);
	int32_t x8, y8, i, j, c_yxb;

	MICROJXL__ASSERT(gx_in_gg % 8 == 0 && gy_in_gg % 8 == 0);

	microjxl__init_code(&code, &f->coeff_codespec[pass]);

	// TODO spec bug: there are *three* NonZeros for each channel
	/* For subsampled channels the nzeros grid is indexed at the subsampled
	 * coordinates (sbx, sby) of each channel, exactly like libjxl's
	 * num_nzeroes planes; the full-res stride is reused so each channel's
	 * subsampled grid occupies a disjoint index range of the same buffer. */
	MICROJXL__TRY_MALLOC(microjxl_i8x3, &nonzeros, (size_t) (gw8 * gh8));

	for (y8 = 0; y8 < gh8; ++y8) for (x8 = 0; x8 < gw8; ++x8) {
		const microjxl__dct_select *dct;
		// TODO spec issue: missing x and y (here called x8 and y8)
		int32_t ggx8 = x8 + gx_in_gg / 8, ggy8 = y8 + gy_in_gg / 8;
		int32_t voff = MICROJXL__I32_PIXELS(&gg->blocks, ggy8)[ggx8], dctsel = voff >> 20;
		int32_t log_rows, log_columns, log_size;
		int32_t coeffoff, qfidx, lfidx, bctx0, bctxc;

		if (dctsel < 2) continue; // not top-left block
		dctsel -= 2;
		voff &= 0xfffff;
		MICROJXL__ASSERT(dctsel < MICROJXL__NUM_DCT_SELECT);
		dct = &MICROJXL__DCT_SELECT[dctsel];
		log_rows = dct->log_rows;
		log_columns = dct->log_columns;
		log_size = log_rows + log_columns;

		coeffoff = gg->varblocks[voff].coeffoff_qfidx & ~15;
		qfidx = gg->varblocks[voff].coeffoff_qfidx & 15;
		// TODO spec improvement: explain why lf_idx is separately calculated
		// (answer: can be efficiently precomputed via vectorization)
		lfidx = MICROJXL__U8_PIXELS(&gg->lfindices, ggy8)[ggx8];
		bctx0 = (dct->order_idx * (f->nb_qf_thr + 1) + qfidx) * lfidx_size + lfidx;
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_ORACLE") && x8 < 6 && y8 < 2) {
			fprintf(stderr, "[mj-bctx] x8=%d y8=%d c=%d lfidx=%d qfidx=%d ord=%d map0=%d\n",
				x8, y8, c_yxb, lfidx, qfidx, dct->order_idx, f->block_ctx_map[bctx0 + bctxc * c_yxb]);
		}
#endif
		bctxc = 13 * (f->nb_qf_thr + 1) * lfidx_size;
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_CTX") && ggx8 == 0 && ggy8 == 0) {
			fprintf(stderr, "[jctx] c=%d ord=%d qfidx=%d lfidx=%d bctx0=%d bctxc=%d\n",
				c_yxb, dct->order_idx, qfidx, lfidx, bctx0, bctxc);
		}
#endif

		// unlike most places, this uses the YXB order
		for (c_yxb = 0; c_yxb < 3; ++c_yxb) {
			static const int32_t YXB2XYB[3] = {1, 0, 2};
			static const int8_t TWICE_COEFF_FREQ_CTX[64] = { // pre-multiplied by 2, [0] is unused
				-1,  0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28,
				30, 30, 32, 32, 34, 34, 36, 36, 38, 38, 40, 40, 42, 42, 44, 44,
				46, 46, 46, 46, 48, 48, 48, 48, 50, 50, 50, 50, 52, 52, 52, 52,
				54, 54, 54, 54, 56, 56, 56, 56, 58, 58, 58, 58, 60, 60, 60, 60,
			};
			// TODO spec bug: CoeffNumNonzeroContext[9] should be 123, not 23
			static const int16_t TWICE_COEFF_NNZ_CTX[64] = { // pre-multiplied by 2
				  0,   0,  62, 124, 124, 186, 186, 186, 186, 246, 246, 246, 246, 304, 304, 304,
				304, 304, 304, 304, 304, 360, 360, 360, 360, 360, 360, 360, 360, 360, 360, 360,
				360, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412,
				412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412, 412,
			};

			int32_t c = YXB2XYB[c_yxb];
			int32_t hshift = f->jpeg_hshift[c], vshift = f->jpeg_vshift[c];
			int32_t sbx = x8 >> hshift, sby = y8 >> vshift;
			float *coeffs = gg->coeffs[c] + coeffoff;
			int32_t *order = f->orders[pass][dct->order_idx][c];
			int32_t			bctx = f->block_ctx_map[bctx0 + bctxc * c_yxb]; // BlockContext()
			int32_t nz, nzctx, cctx, qnz, prev, nzpos_s;

			/* For a subsampled channel, only the blocks whose coordinates are
			 * multiples of 2^hshift / 2^vshift carry coefficients; the others
			 * are simply absent from the bitstream (libjxl skips them in
			 * GetBlockFromBitstream::LoadBlock without consuming any bits). */
			if ((sbx << hshift != x8) || (sby << vshift != y8)) continue;
			nzpos_s = sby * gw8 + sbx;

			// orders should have been already converted from Lehmer code
			MICROJXL__ASSERT(order && ((f->order_loaded >> dct->order_idx) & 1));

			// predict and read the number of non-zero coefficients (prediction
			// happens on the subsampled grid, like libjxl's PredictFromTopAndLeft)
			nz = sbx > 0 ?
				(sby > 0 ? (nonzeros[nzpos_s - 1][c] + nonzeros[nzpos_s - gw8][c] + 1) >> 1 : nonzeros[nzpos_s - 1][c]) :
				(sby > 0 ? nonzeros[nzpos_s - gw8][c] : 32);
			// TODO spec improvement: `predicted` can never exceed 63 in NonZerosContext(),
			// so better to make it a normative assertion instead of clamping
			// TODO spec question: then why the predicted value of 64 is reserved in the contexts?
			MICROJXL__ASSERT(nz < 64);
			nzctx = ctxoff + bctx + (nz < 8 ? nz : 4 + nz / 2) * f->nb_block_ctx;
			nz = microjxl__code(st, nzctx, 0, &code);
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_NZ") && x8 == 0 && y8 == 0) {
				fprintf(stderr, "[jnz] c=%d nz=%d pass=%d dctsel=%d logsize=%d\n", c_yxb, nz, pass, dctsel, log_size);
			}
			if (getenv("MICROJXL_TRACE_ORACLE")) {
				fprintf(stderr, "[oracle-nz] c=%d bx=%d by=%d pred=%d bctx=%d nzctx=%d nz=%d\n",
					YXB2XYB[c_yxb], x8, y8, (int) (sbx > 0 ?
						(sby > 0 ? (nonzeros[nzpos_s - 1][c] + nonzeros[nzpos_s - gw8][c] + 1) >> 1 : nonzeros[nzpos_s - 1][c]) :
						(sby > 0 ? nonzeros[nzpos_s - gw8][c] : 32)), bctx, nzctx, nz);
			}
#endif
			// TODO spec issue: missing
			MICROJXL__SHOULD(nz <= (63 << (log_size - 6)), "coef");

			qnz = microjxl__ceil_div32(nz, 1 << (log_size - 6)); // [0, 64)
			for (i = 0; i < (1 << (log_rows - 3)); ++i) {
				for (j = 0; j < (1 << (log_columns - 3)); ++j) {
					nonzeros[nzpos_s + i * gw8 + j][c] = (int8_t) qnz;
				}
			}
			cctx = ctxoff + 458 * bctx + 37 * f->nb_block_ctx;

			prev = (nz <= (1 << (log_size - 4))); // TODO spec bug: swapped condition					// TODO spec issue: missing size (probably W*H)
					for (i = 1 << (log_size - 6); nz > 0 && i < (1 << log_size); ++i) {
						int32_t ctx = cctx +
							TWICE_COEFF_NNZ_CTX[microjxl__ceil_div32(nz, 1 << (log_size - 6))] +
							TWICE_COEFF_FREQ_CTX[i >> (log_size - 6)] + prev;
						// TODO spec question: can this overflow?
						// unlike modular there is no guarantee about "buffers" or anything similar here
						int32_t ucoeff = microjxl__code(st, ctx, 0, &code);
						// TODO int-to-float conversion, is it okay?
						// libjxl dec_group.cc DecodeACBlock: the pass shift scales the
						// coefficient (magnitude << shift, sign preserved) and passes
						// accumulate into the same buffer.
						int32_t v = microjxl__unpack_signed(ucoeff);
						v = v < 0 ? -((-v) << coef_shift) : (v << coef_shift);
						coeffs[order[i]] += (float) v;
#ifdef MICROJXL_DEBUG
				if (getenv("MICROJXL_TRACE_AC") && ggx8 == 0 && ggy8 == 0) {
					fprintf(stderr, "[jac] c=%d i=%d order=%d ucoeff=%d\n", c_yxb, i, order[i], (int) microjxl__unpack_signed(ucoeff));
				}
#endif
				// TODO spec issue: normative indicator has changed from [[...]] to a long comment
				nz -= prev = (ucoeff != 0);
			}
			MICROJXL__SHOULD(nz == 0, "coef"); // TODO spec issue: missing
		}
	}

	MICROJXL__TRY(microjxl__finish_and_free_code(st, &code));
	microjxl__mem_free(nonzeros);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_code(&code);
	microjxl__mem_free(nonzeros);
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__pass_group(
	microjxl__st *st, int32_t pass, int32_t gx_in_gg, int32_t gy_in_gg, int32_t gw, int32_t gh, int64_t gidx,
	microjxl__lf_group_st *gg
) {
	microjxl__frame_st *f = st->frame;
	/* libjxl frame_header.h Passes::GetDownsamplingBracket: pass `pass`
	 * carries modular channels with min(hshift, vshift) in [minshift,
	 * maxshift] (closed bracket); each later pass tightens the bracket to
	 * [minshift, prev_minshift - 1], and the last pass always includes
	 * shift 0. */
	int32_t minshift, maxshift;
	{
		int32_t p2, j;
		minshift = 3;
		maxshift = 2;
		for (p2 = 0; ; ++p2) {
			for (j = 0; j < f->num_ds; ++j) {
				if (f->ds_last_pass[j] == p2) minshift = f->ds_log[j];
			}
			if (p2 == f->num_passes - 1) minshift = 0;
			if (p2 == pass) break;
			maxshift = minshift - 1;
		}
	}
	// SPEC "the number of tables" is fixed, no matter how many RAW quant tables are there
	int64_t sidx = 1 + 3 * f->num_lf_groups + MICROJXL__NUM_DCT_PARAMS + pass * f->num_groups + gidx;
	microjxl__modular m = MICROJXL__INIT;
	int32_t i;

	if (!f->is_modular) {
		int32_t ctxoff;
		// TODO spec issue: this offset is later referred so should be monospaced
		ctxoff = 495 * f->nb_block_ctx * microjxl__u(st, microjxl__ceil_lg32((uint32_t) f->num_hf_presets));
		MICROJXL__TRY(microjxl__hf_coeffs(st, ctxoff, pass, gx_in_gg, gy_in_gg, gw, gh, gg));
	}

	MICROJXL__TRY(microjxl__init_modular_for_pass_group(st, f->num_gm_channels,
		1 << f->group_size_shift, gg->left + gx_in_gg, gg->top + gy_in_gg,
		minshift, maxshift, &f->gmodular, &m));
	if (m.num_channels > 0) {
		MICROJXL__TRY(microjxl__modular_header(st, f->global_tree, &f->global_codespec, &m));
		MICROJXL__TRY(microjxl__allocate_modular(st, &m));
		for (i = 0; i < m.num_channels; ++i) {
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_STREAM")) fprintf(stderr, "[mjch] pass-group pass=%d g=%d ch=%d sidx=%lld\n", pass, gidx, i, (long long) sidx);
#endif
			MICROJXL__TRY(microjxl__modular_channel(st, &m, i, sidx));
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_PDUMP")) {
				char fn[256];
				snprintf(fn, sizeof(fn), "/tmp/mj_plane_s%lld_c%d.bin", (long long) sidx, i);
				FILE *pf = fopen(fn, "wb");
				if (pf) {
					microjxl__plane *pc = &m.channel[i];
					for (int32_t yy = 0; yy < pc->height; ++yy) {
						if (pc->type == MICROJXL__PLANE_I32) fwrite(MICROJXL__I32_PIXELS(pc, yy), sizeof(int32_t), pc->width, pf);
						else fwrite(MICROJXL__I16_PIXELS(pc, yy), sizeof(int16_t), pc->width, pf);
					}
					fclose(pf);
				}
			}
#endif
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_CHASH")) {
				microjxl__plane *pc = &m.channel[i];
				uint64_t h = 1469598103934665603ull;
				int64_t s = 0; int32_t mn = INT32_MAX, mx = INT32_MIN, xx, yy; size_t n = 0;
				for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
					int32_t v; n++;
					if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx]; else v = MICROJXL__I16_PIXELS(pc, yy)[xx];
					h = (h ^ (uint64_t)(uint32_t)v) * 1099511628211ull;
					s += v; if (v < mn) mn = v; if (v > mx) mx = v;
				}
				fprintf(stderr, "[mjchash] sidx=%lld c=%d w=%d h=%d n=%zu min=%d max=%d sum=%lld hash=%016llx\n",
					(long long) sidx, i, pc->width, pc->height, n, mn, mx, (long long) s, (unsigned long long) h);
			}
#endif
		}
		MICROJXL__TRY(microjxl__finish_and_free_code(st, &m.code));
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_PGDEC")) {
			int32_t cc;
			for (cc = 0; cc < m.num_channels; ++cc) {
				microjxl__plane *pc = &m.channel[cc];
				if (pc->width > 0 && pc->height > 0) {
					int64_t s = 0; int32_t mn = INT32_MAX, mx = INT32_MIN, xx, yy, n = 0;
					for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
						int32_t v; n++;
						if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx]; else v = MICROJXL__I16_PIXELS(pc, yy)[xx];
						s += v; if (v < mn) mn = v; if (v > mx) mx = v;
					}
					fprintf(stderr, "[pgd] preinv ch%d %dx%d: min=%d max=%d mean=%.2f\n", cc, pc->width, pc->height, mn, mx, n ? (double) s / n : 0);
				}
			}
		}
#endif
		MICROJXL__TRY(microjxl__inverse_transform(st, &m));
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_TRACE_PGDEC")) {
			int32_t cc;
			for (cc = 0; cc < m.num_channels; ++cc) {
				microjxl__plane *pc = &m.channel[cc];
				if (pc->width > 0 && pc->height > 0) {
					int64_t s = 0; int32_t mn = INT32_MAX, mx = INT32_MIN, xx, yy, n = 0;
					for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
						int32_t v; n++;
						if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx]; else v = MICROJXL__I16_PIXELS(pc, yy)[xx];
						s += v; if (v < mn) mn = v; if (v > mx) mx = v;
					}
					fprintf(stderr, "[pgd] postinv ch%d %dx%d: min=%d max=%d mean=%.2f\n", cc, pc->width, pc->height, mn, mx, n ? (double) s / n : 0);
				}
			}
		}
#endif
#ifdef MICROJXL_DEBUG
		if (getenv("MICROJXL_GCHASH")) {
			for (i = 0; i < m.num_channels; ++i) {
				microjxl__plane *pc = &m.channel[i];
				uint64_t h = 1469598103934665603ull;
				int64_t s = 0; int32_t mn = INT32_MAX, mx = INT32_MIN, xx, yy; size_t n = 0;
				for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
					int32_t v; n++;
					if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx]; else v = MICROJXL__I16_PIXELS(pc, yy)[xx];
					h = (h ^ (uint64_t)(uint32_t)v) * 1099511628211ull;
					s += v; if (v < mn) mn = v; if (v > mx) mx = v;
				}
				fprintf(stderr, "[mjgchash] sidx=%lld c=%d w=%d h=%d min=%d max=%d sum=%lld hash=%016llx\n",
					(long long) sidx, i, pc->width, pc->height, mn, mx, (long long) s, (unsigned long long) h);
				char pfn[256];
				snprintf(pfn, sizeof(pfn), "/tmp/mj_gplane_%lld_%d.bin", (long long) sidx, i);
				FILE *gpf = fopen(pfn, "wb");
				if (gpf) {
					for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
						int32_t v = 0;
						if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx];
						else if (pc->type == MICROJXL__PLANE_I16) v = MICROJXL__I16_PIXELS(pc, yy)[xx];
						fwrite(&v, 4, 1, gpf);
					}
					fclose(gpf);
				}
			}
		}
#endif
		microjxl__combine_modular_from_pass_group(f->num_gm_channels,
			1 << f->group_size_shift, gg->left + gx_in_gg, gg->top + gy_in_gg,
			minshift, maxshift, &f->gmodular, &m);
		microjxl__mem_free_modular(&m);
	}

	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_modular(&m);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// coefficients to samples

MICROJXL_STATIC void microjxl__dequant_hf(microjxl__st *st, microjxl__lf_group_st *gg);
MICROJXL_STATIC void microjxl__jpeg_recon_capture_ac(microjxl__st *st, microjxl__lf_group_st *gg);
MICROJXL__STATIC_RETURNS_ERR microjxl__combine_vardct_from_lf_group(microjxl__st *st, const microjxl__lf_group_st *gg);
MICROJXL__STATIC_RETURNS_ERR microjxl__gaborish(microjxl__st *st, microjxl__plane channels[3 /*xyb*/]);
MICROJXL__STATIC_RETURNS_ERR microjxl__epf(
	microjxl__st *st, microjxl__plane channels[3], const microjxl__lf_group_st *gg, const microjxl__plane *sigmas_in
);
MICROJXL_STATIC int32_t microjxl__mirror1d(int32_t coord, int32_t size);

/* Convert a custom-width float bit pattern (sign<<(bits-1) | exp<<mant_bits
 * | mantissa, as decoded by the modular pipeline) back to a binary32 float.
 * Mirrors libjxl's int_to_float (dec_modular.cc). For bits==32 the pattern
 * IS a binary32 float (exp_bits is 8 by construction). */
/* binary32 <-> binary16 (IEEE 754 half) conversion, matching libjxl's
 * hwy::F32ToF16/F16ToF32 semantics (round-to-nearest-even on the down
 * conversion; the pipeline rows are half precision on the libjxl side). */
MICROJXL_INLINE uint16_t microjxl__f32_to_f16(float v) {
	uint32_t x;
	uint32_t sign, exp, mant;
	memcpy(&x, &v, sizeof x);
	sign = (x >> 16) & 0x8000u;
	exp = (x >> 23) & 0xffu;
	mant = x & 0x007fffffu;
	if (exp == 0xff) { /* inf/nan */
		return (uint16_t) (sign | 0x7c00u | (mant ? 0x0200u | (mant >> 13) : 0));
	}
	{ /* unbiased exponent re-centred on 15; round-to-nearest-even */
		int32_t e = (int32_t) exp - 127 + 15;
		if (e >= 0x1f) return (uint16_t) (sign | 0x7c00u); /* overflow -> inf */
		if (e <= 0) { /* subnormal or zero */
			if (e < -10) return (uint16_t) sign;
			mant |= 0x00800000u;
			{
				uint32_t shift = (uint32_t) (14 - e);
				uint32_t half = 1u << (shift - 1);
				uint32_t m = mant >> shift;
				uint32_t rem = mant & ((shift >= 32) ? 0xffffffffu : ((1u << shift) - 1u));
				if (rem > half || (rem == half && (m & 1))) m++;
				return (uint16_t) (sign | m);
			}
		}
		{
			uint32_t m = mant >> 13;
			uint32_t rem = mant & 0x1fffu;
			if (rem > 0x1000u || (rem == 0x1000u && (m & 1))) m++;
			if (m == 0x400u) { m = 0; e++; if (e >= 0x1f) return (uint16_t) (sign | 0x7c00u); }
			return (uint16_t) (sign | ((uint32_t) e << 10) | m);
		}
	}
}

MICROJXL_INLINE float microjxl__f16_to_f32(uint16_t h) {
	uint32_t sign = (uint32_t) (h & 0x8000u) << 16;
	uint32_t exp = (h >> 10) & 0x1fu;
	uint32_t mant = h & 0x3ffu;
	uint32_t f;
	if (exp == 0) {
		if (mant == 0) {
			f = sign;
		} else { /* subnormal: normalize */
			int32_t e = -1;
			uint32_t m = mant;
			do { m <<= 1; e++; } while ((m & 0x400u) == 0);
			m &= 0x3ffu;
			f = sign | ((uint32_t) (127 - 15 - e) << 23) | (m << 13);
		}
	} else if (exp == 0x1f) {
		f = sign | 0x7f800000u | (mant << 13);
	} else {
		f = sign | ((exp - 15 + 127) << 23) | (mant << 13);
	}
	{
		float out;
		memcpy(&out, &f, sizeof out);
		return out;
	}
}

MICROJXL_INLINE float microjxl__int_to_float(int32_t ival, int32_t bits, int32_t exp_bits) {
	uint32_t f, mask;
	int32_t exp_bias, sign_shift, mant_bits, mant_shift, exp, mantissa, signbit;
	memcpy(&f, &ival, sizeof f);
	if (bits == 32) {
		float out;
		memcpy(&out, &f, sizeof out);
		return out;
	}
	exp_bias = (1 << (exp_bits - 1)) - 1;
	sign_shift = bits - 1;
	mant_bits = bits - exp_bits - 1;
	mant_shift = 23 - mant_bits;
	signbit = (int32_t) (f >> (uint32_t) sign_shift) & 1;
	mask = ((uint32_t) 1u << (uint32_t) sign_shift) - 1u;
	f &= mask;
	if (f == 0u) return signbit ? -0.0f : 0.0f;
	exp = (int32_t) (f >> (uint32_t) mant_bits);
	mantissa = (int32_t) (f & (((uint32_t) 1u << (uint32_t) mant_bits) - 1u));
	if (exp == (1 << exp_bits) - 1) { /* NaN or infinity */
		f = (uint32_t) (signbit ? 0x80000000u : 0u);
		f |= 0x7f800000u;
		f |= ((uint32_t) mantissa) << (uint32_t) mant_shift;
	} else {
		uint32_t mant = (uint32_t) mantissa << (uint32_t) mant_shift;
		/* subnormal: normalize into binary32 (only when the exponent field
		 * has room; exp_bits==8 can't be subnormal this way) */
		if (exp == 0 && exp_bits < 8) {
			while ((mant & 0x00800000u) == 0u && mant != 0u) {
				mant <<= 1;
				exp--;
			}
			exp++;
			mant &= 0x007fffffu; /* drop the implicit leading 1 */
		}
		exp = exp - exp_bias + 127;
		if (exp < 0) exp = 0; /* underflow guard for extreme subnormals */
		f = (uint32_t) (signbit ? 0x80000000u : 0u);
		f |= ((uint32_t) exp) << 23;
		f |= mant;
	}
	{
		float out;
		memcpy(&out, &f, sizeof out);
		return out;
	}
}

/* The output scale used when storing rendered sample values: float frames
 * (exp_bits != 0) carry values in [0,1] (clamped), so the 8-bit scale is
 * 255; integer frames use (1 << bpp) - 1. bpp is bounded by the fbpp gate
 * for the integer case, so the shift is always in range. */
MICROJXL_INLINE int32_t microjxl__maxpixel_scale(const microjxl__image_st *im) {
	return im->exp_bits != 0 ? 255 : (((int32_t) 1 << im->bpp) - 1);
}

/* Same scale for an extra channel, which may carry its own bit depth
 * (mixed bit depths between colour and alpha are legal and emitted by
 * e.g. cjxl --override_bitdepth with an untouched alpha channel). */
MICROJXL_INLINE int32_t microjxl__maxpixel_alpha(const microjxl__ec_info *ec) {
	return ec->exp_bits != 0 ? 255 : (((int32_t) 1 << ec->bpp) - 1);
}

/* HLG output OOTF (libjxl HlgOOTF::ToSceneLight with desired intensity =
 * intensity_target): per-pixel ratio = luminance^gamma over the primaries'
 * luminances. The luminances are the Y row of the RGB->XYZ matrix built
 * from the chromaticities (jxl_cms PrimariesToXYZ); the per-image folded
 * weights hlg_yeff[c] = sum_i Y[i] * opsin_inv_mat[i][c] let the pixel
 * luminance be computed directly from the pre-inverse-matrix samples. */
static void microjxl__hlg_ootf_init(microjxl__image_st *im) {
	float rx, ry, gx, gy, bx, by, wx, wy;
	float m[3][3], inv[3][3], w[3], xyz[3], Y[3];
	float det, a11, a12, a13, a21, a22, a23, a31, a32, a33;
	int c, i;
	if (im->hlg_init) return;
	im->hlg_init = 1;
	rx = im->cpoints[1][0]; ry = im->cpoints[1][1];
	gx = im->cpoints[2][0]; gy = im->cpoints[2][1];
	bx = im->cpoints[3][0]; by = im->cpoints[3][1];
	wx = im->cpoints[0][0]; wy = im->cpoints[0][1];
	m[0][0] = rx; m[0][1] = gx; m[0][2] = bx;
	m[1][0] = ry; m[1][1] = gy; m[1][2] = by;
	m[2][0] = 1 - rx - ry; m[2][1] = 1 - gx - gy; m[2][2] = 1 - bx - by;
	det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
		- m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
		+ m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	if (wy <= 0 || det == 0 || !isfinite(det)) {
		/* fall back to sRGB luminances */
		Y[0] = 0.2126f; Y[1] = 0.7152f; Y[2] = 0.0722f;
	} else {
		a11 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
		a12 = m[0][2] * m[2][1] - m[0][1] * m[2][2];
		a13 = m[0][1] * m[1][2] - m[0][2] * m[1][1];
		a21 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
		a22 = m[0][0] * m[2][2] - m[0][2] * m[2][0];
		a23 = m[0][2] * m[1][0] - m[0][0] * m[1][2];
		a31 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
		a32 = m[0][1] * m[2][0] - m[0][0] * m[2][1];
		a33 = m[0][0] * m[1][1] - m[0][1] * m[1][0];
		inv[0][0] = a11 / det; inv[0][1] = a12 / det; inv[0][2] = a13 / det;
		inv[1][0] = a21 / det; inv[1][1] = a22 / det; inv[1][2] = a23 / det;
		inv[2][0] = a31 / det; inv[2][1] = a32 / det; inv[2][2] = a33 / det;
		w[0] = wx / wy; w[1] = 1.0f; w[2] = (1 - wx - wy) / wy;
		for (i = 0; i < 3; ++i)
			xyz[i] = inv[i][0] * w[0] + inv[i][1] * w[1] + inv[i][2] * w[2];
		/* RGB->XYZ = primaries * diag(xyz); luminances = its Y row */
		Y[0] = ry * xyz[0];
		Y[1] = gy * xyz[1];
		Y[2] = by * xyz[2];
	}
	for (c = 0; c < 3; ++c) {
		im->hlg_yeff[c] = Y[0] * im->opsin_inv_mat[0][c] +
			Y[1] * im->opsin_inv_mat[1][c] + Y[2] * im->opsin_inv_mat[2][c];
	}
	im->hlg_gamma = (1.0f / 1.2f) * powf(1.111f, -log2f((im->intensity_target > 0.0f ? im->intensity_target : 255.0f) / 1000.0f));
}

/* HlgOOTF_Base::Apply (tone_mapping.h) with HlgOOTF::ToSceneLight(intensity_target):
 * scales display-linear RGB by pow(luminance, gamma-1) before the HLG OETF;
 * gamma-1 is the actual exponent (exponent_), skipped when |gamma-1| <= 0.01.
 * `p0/p1/p2` are the three pre-inverse-matrix display-linear samples; the
 * pixel luminance comes through hlg_yeff (primaries luminances folded through
 * opsin_inv_mat), identical to libjxl's luminance of the display RGB. */
static float microjxl__hlg_ootf_ratio(microjxl__image_st *im, float p0, float p1, float p2) {
	float lum, exponent, ratio;
	if (im->gamma_or_tf != MICROJXL__TF_HLG) return 1.0f;
	microjxl__hlg_ootf_init(im);
	exponent = im->hlg_gamma - 1.0f;
	if (exponent < 0.01f && exponent > -0.01f) return 1.0f; /* !apply_ootf_ */
	lum = im->hlg_yeff[0] * p0 + im->hlg_yeff[1] * p1 + im->hlg_yeff[2] * p2;
	/* libjxl has no luminance clamp here (tone_mapping.h HlgOOTF_Base::Apply):
	 * negative (out-of-gamut) luminance makes powf return NaN, which the
	 * float->int output conversion turns into 0 (black). Mirror that outcome
	 * deterministically instead of clamping to 0, which would make powf
	 * return +inf and explode the channels to white. */
	if (!(lum >= 0.0f)) return 0.0f;
	ratio = powf(lum, exponent);
	return ratio > 1e9f ? 1e9f : ratio;
}

/* encode display-linear [0,1] samples into the image's transfer function
 * (libjxl transfer_functions.h TF_*::EncodedFromDisplay + the ExtraTF stage
 * of the output pipeline). XYB decode produces linear samples; the output
 * must carry the image's own transfer function, not unconditional sRGB. */
static float microjxl__tf_encode(const microjxl__image_st *im, float v) {
	int tf = im->gamma_or_tf;
	if (tf == MICROJXL__TF_LINEAR) return v;
	if (tf == MICROJXL__TF_SRGB || tf == MICROJXL__TF_UNKNOWN) {
		/* libjxl TF_SRGB::EncodedFromDisplay operates on |x| and reapplies
		 * the sign, so out-of-range negatives mirror the curve instead of
		 * extending the linear ramp (which the float conformance output
		 * exposes). The curve itself is NOT evaluated exactly: libjxl's
		 * render pipeline (stage_from_linear.cc OpRgb, JXL_HIGH_PRECISION
		 * path) computes EncodedFromDisplay via a degree-4 rational
		 * polynomial (transfer_functions-inl.h, Horner with fused
		 * multiply-adds) whose deviation from the exact curve is ~2e-5 rms
		 * — the reference conformance output carries that approximation
		 * error, so it must be replicated bit-for-bit to meet the tightest
		 * (2^-18) limits. */
		float a = fabsf(v), e;
		if (a <= 0.0031308f) {
			e = 12.92f * a;
		} else {
			/* EvalRationalPolynomial on sqrt(x): p/q from
			 * TF_SRGB::EncodedFromDisplay */
			float y = sqrtf(a);
			float yp = 7.352629620e-01f;
			yp = fmaf(yp, y, 1.474205315e+00f);
			yp = fmaf(yp, y, 3.903842876e-01f);
			yp = fmaf(yp, y, 5.287254571e-03f);
			yp = fmaf(yp, y, -5.135152395e-04f);
			float yq = 2.424867759e-02f;
			yq = fmaf(yq, y, 9.258482155e-01f);
			yq = fmaf(yq, y, 1.340816930e+00f);
			yq = fmaf(yq, y, 3.036675394e-01f);
			yq = fmaf(yq, y, 1.004519624e-02f);
			e = yp / yq;
		}
		return copysignf(e, v);
	}
	if (tf == MICROJXL__TF_709) {
		return v < 0.018f ? 4.5f * v : 1.099f * powf(v, 0.45f) - 0.099f;
	}
	if (tf == MICROJXL__TF_DCI) {
		/* libjxl mirrors the sign; the [0,1] clamp below handles the rest */
		return v == 0.0f ? 0.0f : copysignf(powf(fabsf(v), 1.0f / 2.6f), v);
	}
	if (tf > 0) { /* gamma, stored *1e7; libjxl OpGamma: encoded = display^gamma */
		float gamma = (float) tf * 1e-7f;
		return v <= 1e-5f ? 0.0f : powf(v, gamma);
	}
	if (tf == MICROJXL__TF_PQ) {
		/* TF_PQ_Base::EncodedFromDisplay(display_intensity_target) */
		const double kM1 = 2610.0 / 16384, kM2 = (2523.0 / 4096) * 128;
		const double kC1 = 3424.0 / 4096, kC2 = (2413.0 / 4096) * 32, kC3 = (2392.0 / 4096) * 32;
		double d = fabs(v), it = im->intensity_target, xp, e;
		if (d == 0.0) return 0.0f;
		xp = pow(d * (it * (1.0 / 10000.0)), kM1);
		e = pow((kC1 + xp * kC2) / (1.0 + xp * kC3), kM2);
		return (float) copysign(e, (double) v);
	}
	/* MICROJXL__TF_HLG: TF_HLG_Base OETF (OOTF is identity at system gamma 1.0) */
	{
		const double kA = 0.17883277, kB = 1 - 4 * kA, kC = 0.5599107295;
		double s = fabs((double) v), e;
		if (s == 0.0) return 0.0f;
		if (s <= 1.0 / 12.0) {
			e = sqrt(3.0 * s);
		} else {
			e = kA * log(12.0 * s - kB) + kC;
		}
		return (float) copysign(e, (double) v);
	}
}

#ifdef MICROJXL_IMPLEMENTATION

/* JPEG reconstruction AC capture (libjxl dec_group.cc DecodeGroupVarDCT,
 * jpeg_data branch). Copies every 8x8 block's raw integer coefficients
 * (which are still intact on entry to dequant_hf) into per-channel JPEG
 * component buffers. JXL stores blocks transposed, so the copy is the
 * implicit Transpose8x8InPlace of libjxl. The CfL back-substitution
 * (jpeg_c = c - y * YtoCRatio) is exact only when the fixed-point factor
 * is 0, which holds exactly for the flat (all-zero) cmap that
 * jpeg-transcoded streams carry; a nonzero tile factor would need
 * libjxl's scaled_qtable path, so those streams degrade (capture off,
 * regular decode unaffected). DC is patched later from the raw LfQuant
 * integers (libjxl's jbrd path uses quantizer.ClearDCMul, making the
 * dequantized DC equal the raw integer * 2^-extra_prec). */
MICROJXL_STATIC void microjxl__jpeg_recon_capture_ac(microjxl__st *st, microjxl__lf_group_st *gg) {
	microjxl__frame_st *f = st->frame;
	microjxl__image_st *im = st->image;
	int32_t ggw8 = gg->width8, ggh8 = gg->height8;
	int32_t x8, y8;
	int is_gray = (f->do_ycbcr == 0 && im->cspace == MICROJXL__CS_GREY);
	if (!f->jpeg_recon) return;
	if (!is_gray && (f->base_corr_x != 0.0f || f->base_corr_b != 0.0f ||
		f->x_factor_lf != 0 || f->b_factor_lf != 0 || f->inv_colour_factor != 1.0f / 84.0f)) {
		/* JPEG-compatible streams have zero DC correlation and colour_factor 84 */
		f->jpeg_recon = 0;
		return;
	}
	/* lazy per-frame allocation + legality checks on the first LF group */
	if (!f->jpeg_recon_alloc) {
		int32_t c, hib[3];
		size_t total[3];
		/* subsampled chroma is fine (libjxl handles it via the !Is444()
		 * direct-copy path); gray propagates channel 1 only. */
		for (c = 0; c < 3; ++c) {
			f->jpeg_wib[c] = microjxl__ceil_div32(microjxl__ceil_div32(f->width, 8), 1 << f->jpeg_hshift[c]);
			f->jpeg_hib[c] = microjxl__ceil_div32(microjxl__ceil_div32(f->height, 8), 1 << f->jpeg_vshift[c]);
			hib[c] = f->jpeg_hib[c];
			total[c] = (size_t) f->jpeg_wib[c] * (size_t) hib[c] * 64;
		}
		for (c = 0; c < 3; ++c) {
			f->jpeg_coeffs[c] = (int16_t *) MICROJXL_MALLOC(total[c] * sizeof(int16_t));
			if (!f->jpeg_coeffs[c]) {
				for (c = 0; c < 3; ++c) microjxl__mem_free(f->jpeg_coeffs[c]);
				f->jpeg_recon = 0;
				return;
			}
			memset(f->jpeg_coeffs[c], 0, total[c] * sizeof(int16_t));
		}
		f->jpeg_recon_alloc = 1;
	}
	{
		/* AcStrategy::Is444(): all channels unsampled */
		int cs444 = !(f->jpeg_hshift[0] || f->jpeg_hshift[2] ||
			f->jpeg_vshift[0] || f->jpeg_vshift[2]);
	for (y8 = 0; y8 < ggh8 && f->jpeg_recon; ++y8) for (x8 = 0; x8 < ggw8; ++x8) {
		int32_t voff = MICROJXL__I32_PIXELS(&gg->blocks, y8)[x8], dctsel = voff >> 20;
		int32_t bx, by, i;
		int32_t factor = 0, ratio = 0; // CfL back-substitution factor (per-block)
		float *cfs[3];
		if (dctsel < 2) continue; // not top-left block
		dctsel -= 2;
		if (dctsel != 0) { // DCT_SELECT[0] is DCT8X8
			f->jpeg_recon = 0; // "Can only decode to JPEG if only DCT-8 is used"
			break;
		}
		voff &= 0xfffff;
		bx = gg->left / 8 + x8;
		by = gg->top / 8 + y8;
		for (i = 0; i < 3; ++i) cfs[i] = gg->coeffs[i] + (gg->varblocks[voff].coeffoff_qfidx & ~15);
		/* per channel in {1, 0, 2} order (libjxl's loop) */
		int16_t *ypos = NULL; // Y block saved for CfL back-substitution
		for (i = 0; i < 3 && f->jpeg_recon; ++i) {
			int32_t c = (i == 0) ? 1 : (i == 1 ? 0 : 2);
			int32_t hshift = f->jpeg_hshift[c], vshift = f->jpeg_vshift[c];
			int32_t sbx = bx >> hshift, sby = by >> vshift;
			int32_t wib = f->jpeg_wib[c];
			int32_t hib = microjxl__ceil_div32(microjxl__ceil_div32(f->height, 8), 1 << vshift);
			int16_t *jpeg_pos;
			int32_t t;
			if (is_gray && c != 1) continue; // propagate only Y for grayscale
			if ((sbx << hshift) != bx || (sby << vshift) != by) continue;
			if (!cs444 && c != 1 && ((bx & 1) || (by & 1))) {
				/* dec_group.cc:405 — with any subsampling, only every second
				 * chroma block has bitstream data (the CfL-substituted grid is
				 * stored at the even coordinates); skip the odd ones. */
				continue;
			}
			if (sbx >= wib || sby >= hib) continue; // padding blocks
			jpeg_pos = f->jpeg_coeffs[c] + ((size_t) sby * (size_t) wib + (size_t) sbx) * 64;
			if (cs444 && c != 1 && !is_gray) {
				/* CfL back-substitution (libjxl dec_group.cc:420-437; only for
				 * Is444 — the !Is444 branch there copies directly): read the
				 * raw cmap tile values for this block (X and B share the tile
				 * grid); when both are 0 the decoded image had no correlation
				 * on this tile, so the X/B blocks copy through directly.
				 * Otherwise kx/ky = value/84 was folded in at encode time and
				 * must be undone block-by-block from the Y coefficients. */
				int32_t cmap_v = gg->xfromy.type == MICROJXL__PLANE_I16 ?
					(int32_t) MICROJXL__I16_PIXELS(&gg->xfromy, y8 / 8)[x8 / 8] :
					(gg->xfromy.type == MICROJXL__PLANE_I32 ?
						MICROJXL__I32_PIXELS(&gg->xfromy, y8 / 8)[x8 / 8] : 0);
				int32_t cmap_vb = gg->bfromy.type == MICROJXL__PLANE_I16 ?
					(int32_t) MICROJXL__I16_PIXELS(&gg->bfromy, y8 / 8)[x8 / 8] :
					(gg->bfromy.type == MICROJXL__PLANE_I32 ?
						MICROJXL__I32_PIXELS(&gg->bfromy, y8 / 8)[x8 / 8] : 0);
				if (cmap_v != 0 || cmap_vb != 0) {
					/* kCFLFixedPointPrecision = 11; RatioJPEG(factor) =
					 * factor * 2048 / 84 (C truncation, chroma_from_luma.h).
					 * scaled_qtable[c][i] = 2048 * qtable[1][i] / qtable[c][i],
					 * stored transposed so scaled_qtable[c][i%8*8+i/8] indexes
					 * the raw (non-transposed) layout. */
					factor = (c == 0) ? cmap_v : cmap_vb;
					ratio = factor * 2048 / 84;
					ypos = f->jpeg_coeffs[1] + ((size_t) (by) * (size_t) f->jpeg_wib[1] + (size_t) bx) * 64;
				}
			}
			if (ratio == 0) {
				for (t = 0; t < 64; ++t) {
					int32_t r = t >> 3, col = t & 7;
					/* JXL coefficients are stored transposed vs JPEG (libjxl's
					 * jbrd path applies Transpose8x8InPlace before writing
					 * jpeg_pos); DC (index 0) is unaffected. */
					int32_t v = (int32_t) cfs[c][col * 8 + r];
					jpeg_pos[t] = (int16_t) v;
				}
			} else {
				/* CfL in JPEG-natural block orientation (inputs transposed
				 * above): out = in + (in_y * coeff_scale + 2^10) >> 11 with
				 * coeff_scale = (qt * ratio + 2^10) >> 11 (all C truncation);
				 * qt at natural position t is the qtable entry at the
				 * transposed index (libjxl stores scaled_qtable transposed,
				 * dec_group.cc:271). Range-check ±4095 like libjxl. */
				for (t = 0; t < 64; ++t) {
					int32_t r = t >> 3, col = t & 7;
					int32_t s = col * 8 + r; /* transposed source index */
					int32_t in = (int32_t) cfs[c][s];
					/* ypos is stored JPEG-natural (capture already transposed),
					 * so the luma coefficient at natural position t is ypos[t]
					 * (libjxl: transposed_dct_y[i] = raw_y[transpose(i)]). */
					int32_t in_y = (int32_t) ypos[t];
					int32_t qt = (2048 * f->jpeg_qtable[64 + s]) / f->jpeg_qtable[c * 64 + s];
					int32_t coeff_scale = (qt * ratio + (1 << 10)) >> 11;
					int32_t cfl = (in_y * coeff_scale + (1 << 10)) >> 11;
					int32_t v = in + cfl;
					if (v > 4095 || v < -4095) { f->jpeg_recon = 0; break; }
					jpeg_pos[t] = (int16_t) v;
				}
			}
			/* DC stays 0 here: microjxl__jpeg_reconstruct patches it from
			 * the raw LfQuant integers (ClearDCMul semantics). */
			jpeg_pos[0] = 0;
		}
	}
	} /* cs444 scope */
}

MICROJXL_STATIC void microjxl__dequant_hf(microjxl__st *st, microjxl__lf_group_st *gg) {
	// QM_SCALE[i] = 0.8^(i - 2)
	static const float QM_SCALE[8] = {1.5625f, 1.25f, 1.0f, 0.8f, 0.64f, 0.512f, 0.4096f, 0.32768f};

	microjxl__frame_st *f = st->frame;
	int32_t ggw8 = gg->width8, ggh8 = gg->height8;
	float x_qm_scale, b_qm_scale, quant_bias_num = st->image->quant_bias_num, *quant_bias = st->image->quant_bias;
	int32_t x8, y8, c, i;

	MICROJXL__ASSERT(f->x_qm_scale >= 0 && f->x_qm_scale < 8);
	MICROJXL__ASSERT(f->b_qm_scale >= 0 && f->b_qm_scale < 8);
	x_qm_scale = QM_SCALE[f->x_qm_scale];
	b_qm_scale = QM_SCALE[f->b_qm_scale];

	for (y8 = 0; y8 < ggh8; ++y8) for (x8 = 0; x8 < ggw8; ++x8) {
		const microjxl__dct_select *dct;
		const microjxl__dq_matrix *dqmat;
		int32_t voff = MICROJXL__I32_PIXELS(&gg->blocks, y8)[x8], dctsel = voff >> 20, size;
		float mult[3 /*xyb*/];

		if (dctsel < 2) continue; // not top-left block
		voff &= 0xfffff;
		dct = &MICROJXL__DCT_SELECT[dctsel - 2];
		size = 1 << (dct->log_rows + dct->log_columns);
		// TODO spec bug: spec says mult[1] = HfMul, should be 2^16 / (global_scale * HfMul)
		mult[1] = 65536.0f / (float) f->global_scale * gg->varblocks[voff].hfmul.inv;
		mult[0] = mult[1] * x_qm_scale;
		mult[2] = mult[1] * b_qm_scale;
		dqmat = &f->dq_matrix[dct->param_idx];
		MICROJXL__ASSERT(dqmat->mode == MICROJXL__DQ_ENC_RAW); // should have been already loaded

		/* JPEG reconstruction capture: grab the raw integer coefficients
		 * (still intact on entry) before the dequant multiply — one hook per
		 * frame (dequant_hf is called once per LF group; the capture checks
		 * gg->left/top and is a no-op afterwards). */
		if (f->jpeg_recon && x8 == 0 && y8 == 0) {
			microjxl__jpeg_recon_capture_ac(st, gg);
			/* the capture may have turned jpeg_recon off (incompatible
			 * stream); dequantization below continues either way so the
			 * regular pixel path is unaffected */
		}

		
		for (c = 0; c < 3; ++c) {
			float *coeffs = gg->coeffs[c] + (gg->varblocks[voff].coeffoff_qfidx & ~15);
			for (i = 0; i < size; ++i) { // LLF positions are left unused and can be clobbered
				// TODO spec issue: "quant" is a variable name and should be monospaced
				if (-1.0f <= coeffs[i] && coeffs[i] <= 1.0f) {
					coeffs[i] *= quant_bias[c]; // TODO coeffs[i] is integer at this point?
				} else {
					coeffs[i] -= quant_bias_num / coeffs[i];
				}
				coeffs[i] *= mult[c] / dqmat->params[i][c]; // TODO precompute this
			}
		}
	}
}

MICROJXL__STATIC_RETURNS_ERR microjxl__combine_vardct_from_lf_group(microjxl__st *st, const microjxl__lf_group_st *gg) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t ggw8 = gg->width8, ggh8 = gg->height8;
	int32_t ggw = gg->width, ggh = gg->height;
	float *scratch = NULL, *scratch2;
	float *ssamples[3] = {0}; // subsampled staging buffer per chroma plane
	int32_t x8, y8, x, y, i, c;

	/* per-block IDCT scratch (max block = 64x64 = 4096 coefficients) */
	MICROJXL__TRY_MALLOC(float, &scratch, 4096);
	MICROJXL__TRY_MALLOC(float, &scratch2, 4096);

	/* libjxl's render pipeline (dec_cache.cc) runs gaborish/EPF as
	 * FRAME-WIDE stages with symmetric borders: filtering sees samples
	 * across group seams. The combine therefore only places the IDCT (and
	 * chroma-subsampled CfL) output into frame-wide float planes; the
	 * filters, patches/splines/noise and the colour conversion run once
	 * per frame in microjxl__finalize_vardct_frame. */
	if (gg->left == 0 && gg->top == 0) {
		for (c = 0; c < 3; ++c) {
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &f->vardct_f[c]));
			for (y = 0; y < f->height; ++y) {
				float *row = MICROJXL__F32_PIXELS(&f->vardct_f[c], y);
				for (x = 0; x < f->width; ++x) row[x] = 0.0f;
			}
		}
		if (f->do_ycbcr || f->jpeg_hshift[0] || f->jpeg_hshift[1] || f->jpeg_hshift[2] ||
			f->jpeg_vshift[0] || f->jpeg_vshift[1] || f->jpeg_vshift[2]) {
			for (c = 0; c < 3; ++c) {
				if (!f->jpeg_hshift[c] && !f->jpeg_vshift[c]) continue;
				f->vardct_ss_w[c] = microjxl__ceil_div32(f->width, 1 << f->jpeg_hshift[c]);
				f->vardct_ss_h[c] = microjxl__ceil_div32(f->height, 1 << f->jpeg_vshift[c]);
				MICROJXL__TRY_MALLOC(float, &f->vardct_ss[c], (size_t) (f->vardct_ss_w[c] * f->vardct_ss_h[c]));
				for (i = 0; i < f->vardct_ss_w[c] * f->vardct_ss_h[c]; ++i) f->vardct_ss[c][i] = 0.0f;
			}
		}

		/* frame-wide EPF tables, gathered from the LF groups (built once;
		 * combine is called per LF group, so allocate on the first) */
		if (f->gab.enabled || f->epf.iters) {
			int32_t sw8 = microjxl__ceil_div32(f->width, 8), sh8 = microjxl__ceil_div32(f->height, 8);
			if (!f->epf_sharp8) {
				MICROJXL__TRY_MALLOC(int32_t, &f->epf_sharp8, (size_t) sw8 * sh8);
				MICROJXL__TRY_MALLOC(int32_t, &f->epf_rowq8, (size_t) sw8 * sh8);
				f->epf_sharp8_w = sw8;
				f->epf_sharp8_h = sh8;
				for (i = 0; i < sw8 * sh8; ++i) { f->epf_sharp8[i] = 0; f->epf_rowq8[i] = 1; }
			}
		}
	}

	/* local aliases of the frame-wide subsampled staging (NULL when no
	 * subsampling); captured AFTER the init above so the first LF group
	 * sees the freshly allocated buffers */
	for (c = 0; c < 3; ++c) ssamples[c] = f->vardct_ss[c];

	for (y8 = 0; y8 < ggh8; ++y8) for (x8 = 0; x8 < ggw8; ++x8) {
		const microjxl__dct_select *dct;
		int32_t voff = MICROJXL__I32_PIXELS(&gg->blocks, y8)[x8], dctsel = voff >> 20;
		int32_t size, effvw, effvh, vw8, vh8, samplepos;
		int32_t coeffoff;
		float *coeffs[3 /*xyb*/], *llfcoeffs[3 /*xyb*/], kx_hf, kb_hf;

		if (dctsel < 2) continue; // not top-left block
		dctsel -= 2;
		voff &= 0xfffff;
		dct = &MICROJXL__DCT_SELECT[dctsel];
		size = 1 << (dct->log_rows + dct->log_columns);
		coeffoff = gg->varblocks[voff].coeffoff_qfidx & ~15;
		for (c = 0; c < 3; ++c) {
			coeffs[c] = gg->coeffs[c] + coeffoff;
			llfcoeffs[c] = gg->llfcoeffs[c] + (coeffoff >> 6);
		}

		/* frame-wide EPF tables: per-8x8 block sharpness and HfMul (used by
		 * the frame-level sigma map in microjxl__finalize_vardct_frame).
		 * libjxl dec_modular.cc writes epf_sharpness for EVERY 8x8 block from
		 * the sharpness plane, and ComputeSigma (epf.cc) propagates the
		 * varblock's row_quant to all covered blocks via the covered_blocks_x/y
		 * loops — each covered block then uses its OWN sharp_lut entry. */
		if (f->epf_sharp8) {
			int32_t bw = f->epf_sharp8_w, bx = gg->left / 8 + x8, by = gg->top / 8 + y8;
			int32_t cw = 1 << (dct->log_columns - 3), ch = 1 << (dct->log_rows - 3), ix, iy;
			for (iy = 0; iy < ch; ++iy) for (ix = 0; ix < cw; ++ix) {
				if (gg->sharpness.type == MICROJXL__PLANE_I16) {
					f->epf_sharp8[(by + iy) * bw + bx + ix] = MICROJXL__I16_PIXELS(&gg->sharpness, y8 + iy)[x8 + ix];
				} else if (gg->sharpness.type == MICROJXL__PLANE_I32) {
					f->epf_sharp8[(by + iy) * bw + bx + ix] = MICROJXL__I32_PIXELS(&gg->sharpness, y8 + iy)[x8 + ix];
				} else {
					f->epf_sharp8[(by + iy) * bw + bx + ix] = 0;
				}
				f->epf_rowq8[(by + iy) * bw + bx + ix] = gg->varblocks[voff].row_quant;
			}
		}


		// TODO spec bug: x_factor and b_factor (for HF) is constant in the same varblock,
		// even when the varblock spans multiple 64x64 rectangles
		kx_hf = f->base_corr_x + f->inv_colour_factor * (gg->xfromy.type == MICROJXL__PLANE_I16 ?
			(float) MICROJXL__I16_PIXELS(&gg->xfromy, y8 / 8)[x8 / 8] :
			(float) MICROJXL__I32_PIXELS(&gg->xfromy, y8 / 8)[x8 / 8]);
		kb_hf = f->base_corr_b + f->inv_colour_factor * (gg->bfromy.type == MICROJXL__PLANE_I16 ?
			(float) MICROJXL__I16_PIXELS(&gg->bfromy, y8 / 8)[x8 / 8] :
			(float) MICROJXL__I32_PIXELS(&gg->bfromy, y8 / 8)[x8 / 8]);

		effvh = microjxl__min32(ggh - y8 * 8, 1 << dct->log_rows);
		effvw = microjxl__min32(ggw - x8 * 8, 1 << dct->log_columns);
		samplepos = (y8 * 8) * ggw + (x8 * 8);
		// this is for LLF coefficients, which may have been transposed
		vh8 = 1 << (microjxl__min32(dct->log_rows, dct->log_columns) - 3);
		vw8 = 1 << (microjxl__max32(dct->log_rows, dct->log_columns) - 3);

		for (c = 0; c < 3; ++c) {
			int32_t hshift = f->jpeg_hshift[c], vshift = f->jpeg_vshift[c];
			/* For a subsampled channel, blocks whose coordinates are not
			 * multiples of 2^hshift / 2^vshift have no coefficients in the
			 * bitstream (they were skipped during the AC decode), so there is
			 * nothing to IDCT: leave those samples at their initial value
			 * (they will be overwritten by the chroma upsampling below). */
			if (((x8 >> hshift) << hshift != x8) || ((y8 >> vshift) << vshift != y8)) continue;

			// chroma from luma (CfL), overwrite LLF coefficients on the way
			// TODO skip CfL if there's subsampled channel
			switch (c) {
			case 0: // X
				for (i = 0; i < size; ++i) scratch[i] = coeffs[0][i] + coeffs[1][i] * kx_hf;
				/* LF correlation is already baked into llfcoeffs (before DC
				 * smoothing, mirroring libjxl's DequantDC); only the per-tile
				 * HF correlation is applied here. */
				for (y = 0; y < vh8; ++y) for (x = 0; x < vw8; ++x) {
					scratch[y * vw8 * 8 + x] = llfcoeffs[0][y * vw8 + x];
				}
				break;
			case 1: // Y
				for (i = 0; i < size; ++i) scratch[i] = coeffs[1][i];
				for (y = 0; y < vh8; ++y) for (x = 0; x < vw8; ++x) {
					scratch[y * vw8 * 8 + x] = llfcoeffs[1][y * vw8 + x];
				}
				break;
			case 2: // B
				for (i = 0; i < size; ++i) scratch[i] = coeffs[2][i] + coeffs[1][i] * kb_hf;
				for (y = 0; y < vh8; ++y) for (x = 0; x < vw8; ++x) {
					scratch[y * vw8 * 8 + x] = llfcoeffs[2][y * vw8 + x];
				}
				break;
			default: MICROJXL__UNREACHABLE();
			}
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_TRACE_SCRATCH") && x8 == 0 && y8 == 0) {
				fprintf(stderr, "[jsc] c=%d llf[0..7]=%g %g %g %g %g %g %g %g  scratch[0..7]=%g %g %g %g %g %g %g %g\n", c,
					(double) llfcoeffs[c][0], (double) llfcoeffs[c][1], (double) llfcoeffs[c][2], (double) llfcoeffs[c][3],
					(double) llfcoeffs[c][4], (double) llfcoeffs[c][5], (double) llfcoeffs[c][6], (double) llfcoeffs[c][7],
					(double) scratch[0], (double) scratch[1], (double) scratch[2], (double) scratch[3],
					(double) scratch[4], (double) scratch[5], (double) scratch[6], (double) scratch[7]);
			}
#endif

			
			// inverse DCT
			switch (dctsel) {
			case 1: microjxl__inverse_hornuss(scratch); break; // Hornuss
			case 2: microjxl__inverse_dct11(scratch); break; // DCT11
			case 3: microjxl__inverse_dct22(scratch); break; // DCT22
			case 12: microjxl__inverse_dct23(scratch); break; // DCT23
			case 13: microjxl__inverse_dct32(scratch); break; // DCT32
			case 14: microjxl__inverse_afv(scratch, 0, 0); break; // AFV0
			case 15: microjxl__inverse_afv(scratch, 1, 0); break; // AFV1
			case 16: microjxl__inverse_afv(scratch, 0, 1); break; // AFV2
			case 17: microjxl__inverse_afv(scratch, 1, 1); break; // AFV3
			default: // every other DCTnm where n, m >= 3
				microjxl__inverse_dct2d(scratch, scratch2, dct->log_rows, dct->log_columns);
				break;
			}

			if (0) { // TODO display borders for the debugging
				for (x = 0; x < (1<<dct->log_columns); ++x) scratch[x] = 1.0f - (float) ((dctsel >> x) & 1);
				for (y = 0; y < (1<<dct->log_rows); ++y) scratch[y << dct->log_columns] = 1.0f - (float) ((dctsel >> y) & 1);
			}

			// reposition samples into the rectangular grid; subsampled channels
			// go into the staging buffer at their (subsampled) block position
			// TODO spec issue: overflown samples (due to non-8n dimensions) are probably ignored
			if (f->jpeg_hshift[c] || f->jpeg_vshift[c]) {
				/* frame-wide staging: sspos is an ABSOLUTE subsampled block
				 * coordinate so 8x8 blocks never alias across LF groups */
				int32_t ssw = f->vardct_ss_w[c];
				int32_t sspos = ((gg->top + y8 * 8) >> f->jpeg_vshift[c]) * ssw
					+ ((gg->left + x8 * 8) >> f->jpeg_hshift[c]);
				for (y = 0; y < effvh; ++y) for (x = 0; x < effvw; ++x) {
					ssamples[c][sspos + y * ssw + x] = scratch[y << dct->log_columns | x];
				}
			} else {
				for (y = 0; y < effvh; ++y) for (x = 0; x < effvw; ++x) {
					MICROJXL__F32_PIXELS(&f->vardct_f[c], gg->top + y8 * 8 + y)[gg->left + x8 * 8 + x] = scratch[y << dct->log_columns | x];
#ifdef MICROJXL_DEBUG
					if (getenv("MICROJXL_TRACE_IDCT") && gg->left == 0 && gg->top == 0 && x8 == 0 && y8 == 0) {
						fprintf(stderr, "[idct] c=%d coeffoff=%d dctsel=%d scratch[0..3]=%g %g %g %g kx_hf=%g kb_hf=%g coeffs0=%g %g coeffs1=%g %g llf0=%g llf1=%g last=%d type=%d\n",
							c, coeffoff, dctsel, (double) scratch[0], (double) scratch[1], (double) scratch[2], (double) scratch[3],
							(double) kx_hf, (double) kb_hf,
							(double) coeffs[0][0], (double) coeffs[0][1], (double) coeffs[1][0], (double) coeffs[1][1],
							(double) llfcoeffs[0][0], (double) llfcoeffs[1][0], (int) f->is_last, (int) f->type);
					}
#endif
				}
			}
		}
	}

	/* subsampled staging is owned by the frame state; nothing to do here */
	(void) ssamples; (void) ggw; (void) ggh;

MICROJXL__ON_ERROR:
	microjxl__mem_free(scratch);
	microjxl__mem_free(scratch2);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// restoration filters

MICROJXL__STATIC_RETURNS_ERR microjxl__gaborish(microjxl__st *st, microjxl__plane channels[3 /*xyb*/]);

MICROJXL_STATIC int32_t microjxl__mirror1d(int32_t coord, int32_t size);
MICROJXL_STATIC void microjxl__epf_distance(const microjxl__plane *in, int32_t dx, int32_t dy, microjxl__plane *out);
MICROJXL__STATIC_RETURNS_ERR microjxl__epf_recip_sigmas(microjxl__st *st, microjxl__plane *out);
MICROJXL__STATIC_RETURNS_ERR microjxl__epf_step(
	microjxl__st *st, microjxl__plane channels[3], float sigma_scale, const microjxl__plane *recip_sigmas,
	int32_t nkernels, const int32_t (*kernels)[2], microjxl__plane (*distances)[3], int dist_uses_cross,
	const microjxl__lf_group_st *gg
);
MICROJXL__STATIC_RETURNS_ERR microjxl__epf(
	microjxl__st *st, microjxl__plane channels[3], const microjxl__lf_group_st *gg, const microjxl__plane *sigmas_in
);

#ifdef MICROJXL_IMPLEMENTATION

// TODO spec issue: restoration filters are applied to the entire image,
// even though their parameters are separately signaled via multiple groups or LF groups!

MICROJXL_MAYBE_UNUSED MICROJXL__STATIC_RETURNS_ERR microjxl__gaborish(microjxl__st *st, microjxl__plane channels[3 /*xyb*/]) {
	microjxl__frame_st *f = st->frame;
	int32_t width, height;
	int32_t c, x, y;
	float *linebuf = NULL, *nline, *line;

	if (!f->gab.enabled) return 0;

	MICROJXL__ASSERT(microjxl__plane_all_equal_sized(channels, channels + 3));
	MICROJXL__ASSERT(microjxl__plane_all_equal_typed(channels, channels + 3) == MICROJXL__PLANE_F32);
	width = channels->width;
	height = channels->height;

	MICROJXL__TRY_MALLOC(float, &linebuf, (size_t) (width * 2));

	for (c = 0; c < 3; ++c) {
		float w0 = 1.0f, w1 = f->gab.weights[c][0], w2 = f->gab.weights[c][1];
		float wsum = w0 + w1 * 4 + w2 * 4;
		MICROJXL__SHOULD(microjxl__surely_nonzero(wsum), "gab0");
		w0 /= wsum; w1 /= wsum; w2 /= wsum;

		nline = linebuf + width; // intentionally uninitialized
		line = linebuf; // row -1 (= row 0 after mirroring)
		memcpy(line, MICROJXL__F32_PIXELS(&channels[c], 0), sizeof(float) * (size_t) width);

		for (y = 0; y < height; ++y) {
			float *sline, *outline, *temp = nline;
			nline = line;
			line = temp;
			sline = y + 1 < height ? MICROJXL__F32_PIXELS(&channels[c], y + 1) : line;
			outline = MICROJXL__F32_PIXELS(&channels[c], y);
			memcpy(line, outline, sizeof(float) * (size_t) width);

			outline[0] =
				nline[0] * (w2 + w1) + nline[1] * w2 +
				 line[0] * (w1 + w0) +  line[1] * w1 +
				sline[0] * (w2 + w1) + sline[1] * w2;
			for (x = 1; x < width - 1; ++x) {
				outline[x] =
					nline[x - 1] * w2 + nline[x] * w1 + nline[x + 1] * w2 +
					 line[x - 1] * w1 +  line[x] * w0 +  line[x + 1] * w1 +
					sline[x - 1] * w2 + sline[x] * w1 + sline[x + 1] * w2;
			}
			if (width > 1) {
				outline[width - 1] =
					nline[width - 2] * w2 + nline[width - 1] * (w1 + w2) +
					 line[width - 2] * w1 +  line[width - 1] * (w0 + w1) +
					sline[width - 2] * w2 + sline[width - 1] * (w1 + w2);
			}
		}
	}

MICROJXL__ON_ERROR:
	microjxl__mem_free(linebuf);
	return st->err;
}

MICROJXL_STATIC int32_t microjxl__mirror1d(int32_t coord, int32_t size) {
	while (1) {
		if (coord < 0) coord = -coord - 1;
		else if (coord >= size) coord = size * 2 - 1 - coord;
		else return coord;
	}
}

// computes out(x + 1, y + 1) = abs(in(x, y) - in(x + dx, y + dy)), up to mirroring.
// used to compute DistanceStep* functions; an increased border is required for correctness.
MICROJXL_STATIC void microjxl__epf_distance(const microjxl__plane *in, int32_t dx, int32_t dy, microjxl__plane *out) {
	int32_t width = in->width, height = in->height;
	int32_t x, y, xlo, xhi;

	MICROJXL__ASSERT(width + 2 == out->width && height + 2 == out->height);
	MICROJXL__ASSERT(in->type == MICROJXL__PLANE_F32 && out->type == MICROJXL__PLANE_F32);
	MICROJXL__ASSERT(-2 <= dx && dx <= 2 && -2 <= dy && dy <= 2);

	xlo = (dx > 0 ? 0 : -dx);
	xhi = (dx < 0 ? width : width - dx);

	// TODO spec issue: `[[(ix, iy) in coords]]` should be normative comments
	// TODO spec issue: `ix` and `iy` not defined in DistanceStep2, should be 0

	for (y = -1; y <= height; ++y) {
		int32_t refy = microjxl__mirror1d(y, height), offy = microjxl__mirror1d(y + dy, height);
		float *refpixels = MICROJXL__F32_PIXELS(in, refy);
		float *offpixels = MICROJXL__F32_PIXELS(in, offy);
		float *outpixels = MICROJXL__F32_PIXELS(out, y + 1) + 1;

		for (x = -1; x < xlo; ++x) {
			outpixels[x] = fabsf(refpixels[microjxl__mirror1d(x, width)] - offpixels[microjxl__mirror1d(x + dx, width)]);
		}
		for (; x < xhi; ++x) {
			outpixels[x] = fabsf(refpixels[x] - offpixels[x + dx]);
		}
		for (; x <= width; ++x) {
			outpixels[x] = fabsf(refpixels[microjxl__mirror1d(x, width)] - offpixels[microjxl__mirror1d(x + dx, width)]);
		}
	}
}

static const float MICROJXL__SIGMA_THRESHOLD = 0.3f;

/* libjxl epf.h: kInvSigmaNum = 4 * (sqrt(0.5) - 1) (negative); it is folded
 * into the stored sigma (1/sigma) so that Weight() = ZeroIfNegative(fma(sad,
 * inv_sigma, 1)) subtracts. kMinSigma = kInvSigmaNum / 0.3 is the per-block
 * skip threshold (sigma < ~0.256). */
#define MICROJXL__EPF_INV_SIGMA_NUM (-1.1715728752538099024f)
#define MICROJXL__EPF_MIN_SIGMA (-3.90524291751269967465540850526868f)

// computes f(sigma) for each block; stores 1/sigma following libjxl ComputeSigma
// (epf.cc) exactly, including the negative kInvSigmaNum convention.
MICROJXL__STATIC_RETURNS_ERR microjxl__epf_recip_sigmas(microjxl__st *st, microjxl__plane *out) {
	microjxl__frame_st *f = st->frame;
	int32_t ggw8 = f->epf_sharp8_w, ggh8 = f->epf_sharp8_h;
	float quant_scale = (float) f->global_scale / 65536.0f;
	int32_t x8, y8;
	uint32_t sharpness_ub = 0;

	/* frame-wide sigma map, gathered from the LF groups during the
	 * combine (libjxl's EPF stage reads frame-decoupled sigma rows) */
	MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, ggw8, ggh8, MICROJXL__PLANE_FORCE_PAD, out));

	for (y8 = 0; y8 < ggh8; ++y8) {
		float *recip_sigmas = MICROJXL__F32_PIXELS(out, y8);
		for (x8 = 0; x8 < ggw8; ++x8) {
			/* libjxl ComputeSigma (epf.cc): row_quant = the block's
			 * raw_quant_field value (HfMul + 1);
			 * sigma_quant = epf_quant_mul / (quant_scale * row_quant *
			 * kInvSigmaNum); sigma = sigma_quant * epf_sharp_lut[];
			 * sigma = min(-1e-4, sigma); stored value = 1/sigma
			 * (negative; Weight() = ZeroIfNegative(fma(sad, inv, 1))). */
			int32_t sharpness = f->epf_sharp8[y8 * ggw8 + x8];
			float sigma_quant, sigma;
			sharpness_ub |= (uint32_t) sharpness;
			sigma_quant = f->epf.quant_mul / (
				quant_scale * (float) f->epf_rowq8[y8 * ggw8 + x8] * MICROJXL__EPF_INV_SIGMA_NUM);
			sigma = sigma_quant * f->epf.sharp_lut[sharpness & 7];
			if (sigma > -1e-4f) sigma = -1e-4f; // std::min(-1e-4f, sigma)
			recip_sigmas[x8] = 1.0f / sigma;
		}
	}
	MICROJXL__SHOULD(sharpness_ub < 8, "shrp");

	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_plane(out);
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__epf_step(
	microjxl__st *st, microjxl__plane channels[3], float sigma_scale, const microjxl__plane *recip_sigmas,
	int32_t nkernels, const int32_t (*kernels)[2], microjxl__plane (*distances)[3], int dist_uses_cross,
	const microjxl__lf_group_st *gg
) {
	enum { NKERNELS = 12 }; // except for the center

	microjxl__frame_st *f = st->frame;
	int32_t ggw8 = gg->width8, ggh8 = gg->height8, width = gg->width, height = gg->height;
	int32_t stride = width + 4, cstride = stride * 3;
	int32_t dstride = MICROJXL__PLANE_STRIDE(&distances[0][0]); /* float stride of the distance planes (width+2) */
	int32_t borderx[4] = {-2, -1, width, width + 1}, mirrorx[4];
	float *linebuf = NULL, *lines[5][3]; // [y+2][c] for row y in the channel c, with mirrored borders
	float *recip_sigmas_for_modular = NULL; // only used for modular
	float border_sigma_scale;
	int32_t x, y, c, k, i;

	MICROJXL__ASSERT(nkernels <= NKERNELS);

	MICROJXL__ASSERT(microjxl__plane_all_equal_sized(channels, channels + 3));
	MICROJXL__ASSERT(microjxl__plane_all_equal_typed(channels, channels + 3) == MICROJXL__PLANE_F32);
	MICROJXL__ASSERT(channels->width == width && channels->height == height);

	if (recip_sigmas) {
		MICROJXL__ASSERT(recip_sigmas->width == ggw8 && recip_sigmas->height == ggh8);
		MICROJXL__ASSERT(recip_sigmas->type == MICROJXL__PLANE_F32);
	} else {
		float recip_sigma;
		MICROJXL__SHOULD(microjxl__surely_nonzero(f->epf.sigma_for_modular), "epf0");

		// sigma is fixed for modular, so if this is below the threshold no filtering happens
		if (f->epf.sigma_for_modular < MICROJXL__SIGMA_THRESHOLD) return 0;

		MICROJXL__TRY_MALLOC(float, &recip_sigmas_for_modular, (size_t) ggw8);
		/* libjxl dec_frame.cc:338: FillImage(kInvSigmaNum / epf_sigma_for_modular)
		 * — NEGATIVE, same convention as the VarDCT path. */
		recip_sigma = MICROJXL__EPF_INV_SIGMA_NUM / f->epf.sigma_for_modular;
		for (x = 0; x < ggw8; ++x) recip_sigmas_for_modular[x] = recip_sigma;
	}

	/* libjxl stage_epf.cc: sm = pass_scale * 1.65; the |kInvSigmaNum| fold
	 * (1.17157...) is already inside the stored 1/sigma, so it must NOT be
	 * multiplied in again here. */
	sigma_scale *= 1.65f;
	border_sigma_scale = sigma_scale * f->epf.border_sad_mul;

	for (c = 0; c < 3; ++c) {
		for (k = 0; k < nkernels; ++k) {
			/* kernels[] holds (row, col) offsets; epf_distance takes (dx=col,
			 * dy=row) — swapping them assigns each neighbor another neighbor's
			 * SAD (libjxl stage_epf.cc computes the SAD toward the neighbor
			 * itself). */
			microjxl__epf_distance(&channels[c], kernels[k][1], kernels[k][0], &distances[k][c]);
		}
	}

	for (i = 0; i < 4; ++i) mirrorx[i] = microjxl__mirror1d(borderx[i], width);

	MICROJXL__TRY_MALLOC(float, &linebuf, (size_t) (cstride * 4));
	for (c = 0; c < 3; ++c) {
		int32_t ym2 = microjxl__mirror1d(-2, height), ym1 = microjxl__mirror1d(-1, height);
		/* Each channel gets 4 lines of `stride` floats (lines[4][c] is assigned
		 * an external row pointer in the row loop, never linebuf). The base of
		 * channel c is therefore 4*stride*c, not cstride*c (3*stride) which
		 * would overlap adjacent channels. Within a line, pixels start at +2
		 * so the two left border slots (borderx = -2, -1) are addressable
		 * below pixel 0, and the two right border slots (width, width+1)
		 * fit inside the stride. */
		for (i = 0; i < 4; ++i) lines[i][c] = linebuf + 4 * stride * c + stride * i + 2;
		memcpy(lines[1][c], MICROJXL__F32_PIXELS(&channels[c], ym2), sizeof(float) * (size_t) width);
		memcpy(lines[2][c], MICROJXL__F32_PIXELS(&channels[c], ym1), sizeof(float) * (size_t) width);
		memcpy(lines[3][c], MICROJXL__F32_PIXELS(&channels[c], 0), sizeof(float) * (size_t) width);
		for (i = 0; i < 4; ++i) {
			int32_t borderpos = borderx[i], mirrorpos = mirrorx[i];
			lines[1][c][borderpos] = lines[1][c][mirrorpos];
			lines[2][c][borderpos] = lines[2][c][mirrorpos];
			lines[3][c][borderpos] = lines[3][c][mirrorpos];
		}
	}

	for (y = 0; y < height; ++y) {
		int32_t y1 = microjxl__mirror1d(y + 1, height), y2 = microjxl__mirror1d(y + 2, height);
		float *outline[3];
		float *recip_sigma_row =
			recip_sigmas ? MICROJXL__F32_PIXELS(recip_sigmas, y / 8) : recip_sigmas_for_modular;
		float *distance_rows[NKERNELS][3][3] = {{{0}}}; // [kernel_idx][dy+1][c]

		for (c = 0; c < 3; ++c) {
			float *temp = lines[0][c];
			lines[0][c] = lines[1][c];
			lines[1][c] = lines[2][c];
			lines[2][c] = lines[3][c];
			lines[3][c] = temp;
			lines[4][c] = MICROJXL__F32_PIXELS(&channels[c], y2);
			outline[c] = MICROJXL__F32_PIXELS(&channels[c], y);

			memcpy(lines[3][c], MICROJXL__F32_PIXELS(&channels[c], y1), sizeof(float) * (size_t) width);
			for (i = 0; i < 4; ++i) lines[3][c][borderx[i]] = lines[3][c][mirrorx[i]];

			for (k = 0; k < nkernels; ++k) {
				for (i = 0; i < 3; ++i) {
					distance_rows[k][i][c] = MICROJXL__F32_PIXELS(&distances[k][c], y + i);
				}
			}
		}

		for (x = 0; x < width; ++x) {
			float recip_sigma = recip_sigma_row[x / 8], inv_sigma_times_pos_mult;
			float sum_weights, sum_channels[3];

			/* stored 1/sigma is negative (libjxl convention); blocks with
			 * |sigma| below ~0.256 (row_sigma < kMinSigma = -3.905) are
			 * skipped entirely (stage_epf.cc:121/262/...). */
			if (recip_sigma < MICROJXL__EPF_MIN_SIGMA) {
				x += 7; // this and at most 7 subsequent pixels will be skipped anyway
				continue;
			}

			// TODO spec issue: "either coordinate" refers to both x and y (i.e. "borders")
			// according to the source code
			/* libjxl stage_epf.cc: the border sad_mul applies when the pixel is in
			 * the first/last column of an 8x8 block (sad_mul_center[0]/[7] = bsm)
			 * OR the first/last row (whole sad_mul_border row). Per-axis that is
			 * (coord & 7) < 2 for (x+1)/(y+1); ORing the coordinates before the
			 * mask (the previous code) missed most combinations, e.g. x=7,y=3. */
			if (((x + 1) & 7) < 2 || ((y + 1) & 7) < 2) {
				inv_sigma_times_pos_mult = recip_sigma * border_sigma_scale;
			} else {
				inv_sigma_times_pos_mult = recip_sigma * sigma_scale;
			}

			// kernels[*] do not include center, which distance is always 0
			sum_weights = 1.0f;
			for (c = 0; c < 3; ++c) sum_channels[c] = lines[2][c][x];

			/* libjxl's exact float-op order matters for last-ULP parity:
			 * - Weight = ZeroIfNegative(fmaf(sad, inv_sigma, 1)); stored 1/sigma
			 *   is NEGATIVE so a bigger SAD lowers the weight (Weight(),
			 *   stage_epf.cc:47-51).
			 * - Pixel accumulation order: top, left, right, bottom, via
			 *   fmaf(w, px, acc); final X * (1/w) with a true division (Div,
			 *   stage_epf.cc:178 under JXL_HIGH_PRECISION). */
			if (dist_uses_cross) {
				/* distance planes hold |v(p) - v(p + N)| pointwise; the SAD for
				 * neighbor N is the sum of the five plus offsets around the ORIGIN
				 * pixel of that pointwise plane (libjxl sads_off rows p22 +- plus).
				 * Addition order is float-visible: EPF0 (stage_epf.cc:172 plus_off)
				 * = origin, up, left, down, right; EPF1 (unrolled sad0c..sad3c)
				 * = up, left, origin, right, down. */
				for (k = 0; k < nkernels; ++k) {
					float dist = 0.0f;
					for (c = 0; c < 3; ++c) {
						const float *drow = distance_rows[k][1][c] + x + 1;
						float s;
						if (dist_uses_cross == 1) { // EPF0 order
							s = drow[0];
							s = s + drow[-dstride];
							s = s + drow[-1];
							s = s + drow[dstride];
							s = s + drow[1];
						} else { // EPF1 order
							s = drow[-dstride];
							s = s + drow[-1];
							s = s + drow[0];
							s = s + drow[1];
							s = s + drow[dstride];
						}
						dist = microjxl__fmaf(f->epf.channel_scale[c], s, dist);
					}
					float weight = microjxl__maxf(0.0f, microjxl__fmaf(dist, inv_sigma_times_pos_mult, 1.0f));
					sum_weights += weight;
					for (c = 0; c < 3; ++c) {
						sum_channels[c] = microjxl__fmaf(weight, lines[2 + kernels[k][0]][c][x + kernels[k][1]], sum_channels[c]);
					}
				}
			} else {
				/* EPF2: sad = Mul(scale[0], d0), then MulAdd(scale[1], d1, .),
				 * MulAdd(scale[2], d2, .) — first channel NOT fused. */
				for (k = 0; k < nkernels; ++k) {
					float dist;
					dist = f->epf.channel_scale[0] * distance_rows[k][1][0][x + 1];
					dist = microjxl__fmaf(f->epf.channel_scale[1], distance_rows[k][1][1][x + 1], dist);
					dist = microjxl__fmaf(f->epf.channel_scale[2], distance_rows[k][1][2][x + 1], dist);
					float weight = microjxl__maxf(0.0f, microjxl__fmaf(dist, inv_sigma_times_pos_mult, 1.0f));
					sum_weights += weight;
					for (c = 0; c < 3; ++c) {
						sum_channels[c] = microjxl__fmaf(weight, lines[2 + kernels[k][0]][c][x + kernels[k][1]], sum_channels[c]);
					}
				}
			}

			for (c = 0; c < 3; ++c) outline[c][x] = sum_channels[c] * (1.0f / sum_weights);
		}
	}

MICROJXL__ON_ERROR:
	microjxl__mem_free(recip_sigmas_for_modular);
	microjxl__mem_free(linebuf);
	return st->err;
}

MICROJXL_MAYBE_UNUSED MICROJXL__STATIC_RETURNS_ERR microjxl__epf(
	microjxl__st *st, microjxl__plane channels[3], const microjxl__lf_group_st *gg, const microjxl__plane *sigmas_in
) {
	/* (row, col) offsets, matching libjxl stage_epf.cc sads_off verbatim:
	 * each neighbor N gets the SAD summed over the plus-shaped offsets
	 * around the ORIGIN pixel of |v[off] - v[off + N]| (epf_distance
	 * supplies the pointwise |v(p) - v(p + N)| planes; the cross sum below
	 * adds the five plus offsets around the origin). Order is irrelevant. */
	static const int32_t KERNELS12[][2] = {
		{-2,0}, {-1,-1}, {-1,0}, {-1,1}, {0,-2}, {0,-1}, {0,1}, {0,2}, {1,-1}, {1,0}, {1,1}, {2,0},
	}, KERNELS4[][2] = { // 0 < L1 distance <= 1 (steps 1 and 2)
		/* accumulation order top, left, right, bottom (libjxl EPF1/EPF2
		 * AddPixel sequence — float-visible). */
		{-1,0}, {0,-1}, {0,1}, {1,0},
	};

	microjxl__frame_st *f = st->frame;
	microjxl__plane recip_sigmas_ = MICROJXL__INIT, *recip_sigmas;
	microjxl__plane distances[12][3] = MICROJXL__INIT;
	int32_t k, c, maxnkernels = 0;

	if (f->epf.iters <= 0) return 0;

	if (!f->is_modular && !sigmas_in) {
		recip_sigmas = &recip_sigmas_;
		MICROJXL__TRY(microjxl__epf_recip_sigmas(st, recip_sigmas));
	} else {
		recip_sigmas = (microjxl__plane *) sigmas_in; // NULL for modular frames
	}

	// TODO the current implementation takes up to 36 times the input image size of memory!
	maxnkernels = f->epf.iters >= 3 ? 12 : 4;
	for (k = 0; k < maxnkernels; ++k) for (c = 0; c < 3; ++c) {
		MICROJXL__TRY(microjxl__init_plane(
			st, MICROJXL__PLANE_F32, channels[c].width + 2, channels[c].height + 2, 0, &distances[k][c]));
	}

	if (f->epf.iters >= 3) { // step 0
		MICROJXL__TRY(microjxl__epf_step(
			st, channels, f->epf.pass0_sigma_scale, recip_sigmas, 12, KERNELS12, distances, 1, gg));
	}
	if (f->epf.iters >= 1) { // step 1
		MICROJXL__TRY(microjxl__epf_step(st, channels, 1.0f, recip_sigmas, 4, KERNELS4, distances, 2, gg));
	}
	if (f->epf.iters >= 2) { // step 2
		MICROJXL__TRY(microjxl__epf_step(
			st, channels, f->epf.pass2_sigma_scale, recip_sigmas, 4, KERNELS4, distances, 0, gg));
	}

MICROJXL__ON_ERROR:
	if (recip_sigmas) microjxl__mem_free_plane(recip_sigmas);
	for (k = 0; k < maxnkernels; ++k) for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&distances[k][c]);
	return st->err;
}

#endif // MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// frame parsing primitives

struct microjxl__group_info {
	int64_t ggidx;
	int32_t gx_in_gg, gy_in_gg;
	int32_t gw, gh;
};

typedef struct {
	microjxl__st *parent; // can be NULL if not initialized
	microjxl__st st;
	microjxl__buffer_st buffer;
} microjxl__section_st;

MICROJXL__STATIC_RETURNS_ERR microjxl__allocate_lf_groups(microjxl__st *st, microjxl__lf_group_st **out);
MICROJXL__STATIC_RETURNS_ERR microjxl__prepare_dq_matrices(microjxl__st *st);
MICROJXL__STATIC_RETURNS_ERR microjxl__prepare_orders(microjxl__st *st);
MICROJXL_ALWAYS_INLINE struct microjxl__group_info microjxl__group_info(microjxl__frame_st *f, int64_t gidx);

MICROJXL__STATIC_RETURNS_ERR microjxl__init_section_state(
	microjxl__st **stptr, microjxl__section_st *sst, int64_t codeoff, int32_t size
);
MICROJXL__STATIC_RETURNS_ERR microjxl__finish_section_state(microjxl__st **stptr, microjxl__section_st *sst, microjxl_err err);

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_global_in_section(microjxl__st *st, const microjxl__toc *toc);
MICROJXL__STATIC_RETURNS_ERR microjxl__hf_global_in_section(microjxl__st *st, const microjxl__toc *toc);
MICROJXL__STATIC_RETURNS_ERR microjxl__lf_or_pass_group_in_section(microjxl__st *st, microjxl__toc *toc, microjxl__lf_group_st *ggs);

MICROJXL__STATIC_RETURNS_ERR microjxl__combine_vardct(microjxl__st *st, microjxl__lf_group_st *ggs);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__allocate_lf_groups(microjxl__st *st, microjxl__lf_group_st **out) {
	microjxl__frame_st *f = st->frame;
	microjxl__lf_group_st *ggs = NULL;
	int32_t ggsize = 8 << f->group_size_shift, gsize = 1 << f->group_size_shift;
	int32_t ggx, ggy, ggidx = 0, gidx = 0, gstride = microjxl__ceil_div32(f->width, gsize);

	MICROJXL__TRY_CALLOC(microjxl__lf_group_st, &ggs, (size_t) f->num_lf_groups);

	for (ggy = 0; ggy < f->height; ggy += ggsize) {
		int32_t ggh = microjxl__min32(ggsize, f->height - ggy);
		int32_t grows = microjxl__ceil_div32(ggh, gsize);
		for (ggx = 0; ggx < f->width; ggx += ggsize, ++ggidx) {
			microjxl__lf_group_st *gg = &ggs[ggidx];
			int32_t ggw = microjxl__min32(ggsize, f->width - ggx);
			int32_t gcolumns = microjxl__ceil_div32(ggw, gsize);
			gg->idx = ggidx;
			gg->left = ggx; gg->top = ggy;
			gg->width = ggw; gg->height = ggh;
			gg->width8 = microjxl__ceil_div32(ggw, 8); gg->height8 = microjxl__ceil_div32(ggh, 8);
			gg->width64 = microjxl__ceil_div32(ggw, 64); gg->height64 = microjxl__ceil_div32(ggh, 64);
			gg->gidx = gidx + (ggx >> f->group_size_shift);
			gg->grows = grows;
			gg->gcolumns = gcolumns;
			gg->gstride = gstride;
		}
		gidx += grows * gstride;
	}

	MICROJXL__ASSERT(f->num_lf_groups == ggidx);
	MICROJXL__ASSERT(f->num_groups == gidx);
	*out = ggs;
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__prepare_dq_matrices(microjxl__st *st) {
	microjxl__frame_st *f = st->frame;
	int32_t dct_select_not_loaded = f->dct_select_used & ~f->dct_select_loaded;
	int32_t i;
	if (!dct_select_not_loaded) return 0;
	for (i = 0; i < MICROJXL__NUM_DCT_SELECT; ++i) {
		if (dct_select_not_loaded >> i & 1) {
			const microjxl__dct_select *dct = &MICROJXL__DCT_SELECT[i];
			int32_t param_idx = dct->param_idx;
			MICROJXL__TRY(microjxl__load_dq_matrix(st, param_idx, &f->dq_matrix[param_idx]));
			f->dct_select_loaded |= 1 << i;
		}
	}
	/* JPEG reconstruction: the qtable integers were captured in
	 * microjxl__read_dq_matrix's RAW branch (microjxl__jpeg_recon_capture_qtable);
	 * if no 8x8 RAW table was read at all, the stream is not reconstructible. */
	if (f->jpeg_recon && !f->jpeg_qtable_ok) f->jpeg_recon = 0;
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__prepare_orders(microjxl__st *st) {
	microjxl__frame_st *f = st->frame;
	int32_t order_not_loaded = f->order_used & ~f->order_loaded;
	int32_t pass, i, c;
	if (!order_not_loaded) return 0;
	for (i = 0; i < MICROJXL__NUM_ORDERS; ++i) {
		if (order_not_loaded >> i & 1) {
			int32_t log_rows = MICROJXL__LOG_ORDER_SIZE[i][0];
			int32_t log_columns = MICROJXL__LOG_ORDER_SIZE[i][1];
			int32_t *order, temp, skip = 1 << (log_rows + log_columns - 6);
			for (pass = 0; pass < f->num_passes; ++pass) for (c = 0; c < 3; ++c) {
				MICROJXL__TRY(microjxl__natural_order(st, log_rows, log_columns, &order));
				microjxl__apply_permutation(order + skip, &temp, sizeof(int32_t), f->orders[pass][i][c]);
				microjxl__mem_free(f->orders[pass][i][c]);
				f->orders[pass][i][c] = order;
			}
			f->order_loaded |= 1 << i;
		}
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL_ALWAYS_INLINE struct microjxl__group_info microjxl__group_info(microjxl__frame_st *f, int64_t gidx) {
	struct microjxl__group_info info;
	int32_t shift = f->group_size_shift;
	int64_t row, column;
	MICROJXL__ASSERT(0 <= gidx && gidx < f->num_groups);
	row = gidx / f->gcolumns;
	column = gidx % f->gcolumns;
	info.ggidx = (row / 8) * f->ggcolumns + (column / 8);
	info.gx_in_gg = (int32_t) (column % 8) << shift;
	info.gy_in_gg = (int32_t) (row % 8) << shift;
	info.gw = (int32_t) (microjxl__min64(f->width, (column + 1) << shift) - (column << shift));
	info.gh = (int32_t) (microjxl__min64(f->height, (row + 1) << shift) - (row << shift));
	return info;
}

// creates a new per-section state `sst` which is identical to `*stptr` except for `buffer`,
// then ensures that only codestream offsets [codeoff, codeoff + size) are available to `sst`
// and updates `stptr` to point to `sst`, which should be restored with `microjxl__finish_section_state`.
MICROJXL__STATIC_RETURNS_ERR microjxl__init_section_state(
	microjxl__st **stptr, microjxl__section_st *sst, int64_t codeoff, int32_t size
) {
	static const microjxl__buffer_st BUFFER_INIT = MICROJXL__INIT;
	microjxl__st *st = *stptr;
	int64_t fileoff, codeoff_limit;

	sst->parent = NULL;

	MICROJXL__ASSERT(codeoff <= INT64_MAX - size);
	MICROJXL__TRY(microjxl__map_codestream_offset(st, codeoff, &fileoff));
	MICROJXL__SHOULD(microjxl__add64(codeoff, size, &codeoff_limit), "flen");

	MICROJXL__TRY(microjxl__seek_from_source(st, fileoff)); // doesn't alter st->buffer

	sst->st = *st;
	sst->buffer = BUFFER_INIT;
	sst->st.buffer = &sst->buffer;
	MICROJXL__TRY(microjxl__init_buffer(&sst->st, codeoff, codeoff_limit));

MICROJXL__ON_ERROR:
	sst->parent = st;
	*stptr = &sst->st;
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__finish_section_state(microjxl__st **stptr, microjxl__section_st *sst, microjxl_err err) {
	microjxl__st *st;

	if (!sst->parent) return err;
	MICROJXL__ASSERT(*stptr == &sst->st);

	if (err) {
		*stptr = st = sst->parent;
		MICROJXL__ASSERT(sst->st.err == err);
		st->err = err;
		st->saved_errno = sst->st.saved_errno;
		st->cannot_retry = sst->st.cannot_retry;
		// TODO `shrt` is not recoverable if this section is not the last section read
	} else {
		st = &sst->st;
		MICROJXL__ASSERT(!st->err);
		MICROJXL__TRY(microjxl__no_more_bytes(st));
	}

MICROJXL__ON_ERROR:
	*stptr = st = sst->parent;
	microjxl__mem_free_buffer(&sst->buffer);

	// ensure that other subsystems can't be accidentally deallocated
	sst->parent = NULL;
	sst->st.source = NULL;
	sst->st.container = NULL;
	sst->st.buffer = NULL;
	sst->st.image = NULL;
	sst->st.frame = NULL;

	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_global_in_section(microjxl__st *st, const microjxl__toc *toc) {
	microjxl__section_st sst = MICROJXL__INIT;
	if (!toc->single_size) {
		MICROJXL__TRY(microjxl__init_section_state(&st, &sst, toc->lf_global_codeoff, toc->lf_global_size));
	}
	MICROJXL__TRY(microjxl__finish_section_state(&st, &sst, microjxl__lf_global(st)));
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__hf_global_in_section(microjxl__st *st, const microjxl__toc *toc) {
	microjxl__section_st sst = MICROJXL__INIT;
	if (st->frame->is_modular) {
		MICROJXL__SHOULD(toc->hf_global_size == 0, "excs");
	} else {
		if (!toc->single_size) {
#ifdef MICROJXL_DEBUG
			fprintf(stderr, "[microjxl] hf_global section: off=%lld size=%d single=%d\n", (long long) toc->hf_global_codeoff, toc->hf_global_size, toc->single_size);
#endif
			MICROJXL__TRY(microjxl__init_section_state(&st, &sst, toc->hf_global_codeoff, toc->hf_global_size));
		}
		MICROJXL__TRY(microjxl__finish_section_state(&st, &sst, microjxl__hf_global(st)));
	}
MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__lf_or_pass_group_in_section(microjxl__st *st, microjxl__toc *toc, microjxl__lf_group_st *ggs) {
	microjxl__section section = toc->sections[toc->nsections_read];
	microjxl__section_st sst = MICROJXL__INIT;

	if (section.pass < 0) { // LF group
		microjxl__lf_group_st *gg = &ggs[section.idx];
		MICROJXL__TRY(microjxl__init_section_state(&st, &sst, section.codeoff, section.size));
		MICROJXL__TRY(microjxl__finish_section_state(&st, &sst, microjxl__lf_group(st, gg)));
		gg->loaded = 1;
		MICROJXL__TRY(microjxl__prepare_dq_matrices(st));
		MICROJXL__TRY(microjxl__prepare_orders(st));
	} else { // pass group
		struct microjxl__group_info info = microjxl__group_info(st->frame, section.idx);
		microjxl__lf_group_st *gg = &ggs[info.ggidx];
		MICROJXL__ASSERT(gg->loaded); // microjxl__read_toc should have taken care of this
		MICROJXL__TRY(microjxl__init_section_state(&st, &sst, section.codeoff, section.size));
		MICROJXL__TRY(microjxl__finish_section_state(&st, &sst, microjxl__pass_group(
			st, section.pass, info.gx_in_gg, info.gy_in_gg, info.gw, info.gh, section.idx, gg)));
	}

	++toc->nsections_read;

MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__combine_vardct(microjxl__st *st, microjxl__lf_group_st *ggs) {
	microjxl__frame_st *f = st->frame;
	int64_t i;

	/* The rendered sRGB samples are unsigned 16-bit (0..65535), which does
	 * not fit in an int16_t plane without wrapping (values above 32767 would
	 * decode as negative), so always use 32-bit planes here, mirroring the
	 * !modular_16bit_buffers path of the modular renderer.
	 * For grayscale frames the VarDCT coefficients still carry three XYB
	 * planes (X=0, B=Y for gray), so the XYB->RGB math below produces
	 * equal R=G=B automatically -- no gray-specific handling is needed
	 * in the combine itself. */
	{
		/* For VarDCT frames the colour channels are not part of the modular
		 * image (they come from the DCT coefficients), so every channel
		 * decoded by lf_global is an extra channel (alpha etc.). Preserve
		 * them: the combine below only fills the three colour planes and
		 * would otherwise destroy the decoded extra-channel data. */
		int32_t nb_extra = f->gmodular.num_channels;
		microjxl__plane *extra = NULL;
		if (nb_extra > 0) {
			MICROJXL__TRY_MALLOC(microjxl__plane, &extra, (size_t) nb_extra);
			memcpy(extra, f->gmodular.channel, sizeof(microjxl__plane) * (size_t) nb_extra);
			microjxl__mem_free(f->gmodular.channel);
		}
		f->gmodular.num_channels = 3 + nb_extra;
		MICROJXL__TRY_CALLOC(microjxl__plane, &f->gmodular.channel, (size_t) f->gmodular.num_channels);
		for (i = 0; i < 3; ++i) {
			MICROJXL__TRY(microjxl__init_plane(
				st, MICROJXL__PLANE_I32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &f->gmodular.channel[i]));
		}
		if (nb_extra > 0) {
			memcpy(&f->gmodular.channel[3], extra, sizeof(microjxl__plane) * (size_t) nb_extra);
			microjxl__mem_free(extra);
		}
	}
	/* Noise is applied post-upsampling at final resolution (libjxl stage
	 * order: UpsamplingStage -> ConvolveNoise/AddNoise); defer it to
	 * finalize_xyb_color for upsampled XYB VarDCT frames. YCbCr frames
	 * keep the stored-resolution generation (their conversion is linear,
	 * so the stage order does not matter there). */
	if (f->has_noise && (f->do_ycbcr || f->log_upsampling == 0)) MICROJXL__TRY(microjxl__add_noise_frame(st));
	/* reference snapshot (K.3): frames saved before the colour transform
	 * keep the pre-merge correlated (px, py, pb) floats, filled by the
	 * combine pixel loop below (libjxl saves the pipeline rows at the
	 * save-before-colour-transform point, i.e. post-noise). Upsampled XYB
	 * VarDCT frames reuse the snapshot as the deferred-conversion carrier:
	 * the floats are upsampled by upsample_frame and converted to the
	 * transfer function by finalize_xyb_color at final resolution. */
	if ((f->save_before_ct && microjxl__frame_can_ref(f)) ||
		(!f->do_ycbcr && f->log_upsampling > 0) ||
		/* LF frames capture their samples here too (decode_lf_frame moves
		 * the planes into im->lf_frames): libjxl saves DC frames
		 * pre-colour-transform (dec_frame.cc forces save_before_ct), i.e.
		 * exactly at the combine's ref_snap fill point. */
		f->type == MICROJXL__FRAME_LF) {
		for (i = 0; i < 3; ++i) {
			MICROJXL__TRY(microjxl__init_plane(
				st, MICROJXL__PLANE_F32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &f->ref_snap[i]));
		}
	}
	/* Frame-wide adaptive DC smoothing: libjxl FinalizeDC smooths the
	 * assembled frame-wide DC image between ProcessDCGroup and
	 * ProcessACGroup; the LLF coefficients seeded from LfQuant are reseeded
	 * from the smoothed values (libjxl's dc_rows comes from the smoothed
	 * image) BEFORE the combine places IDCT + LLF into the frame planes. */
	MICROJXL__TRY(microjxl__smooth_lf_frame(st, ggs));
	for (i = 0; i < f->num_lf_groups; ++i) {
		microjxl__dequant_hf(st, &ggs[i]);
		MICROJXL__TRY(microjxl__combine_vardct_from_lf_group(st, &ggs[i]));
	}

MICROJXL__ON_ERROR:
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__end_of_frame(microjxl__st *st, const microjxl__toc *toc) {
	MICROJXL__TRY(microjxl__zero_pad_to_byte(st));
	if (toc->single_size) {
		int64_t codeoff = microjxl__codestream_offset(st);
		if (codeoff < toc->end_codeoff) {
			st->cannot_retry = 1;
			MICROJXL__RAISE("shrt");
		} else {
			MICROJXL__SHOULD(codeoff == toc->end_codeoff, "excs");
		}
	} else {
		MICROJXL__TRY(microjxl__seek_buffer(st, toc->end_codeoff));
	}
MICROJXL__ON_ERROR:
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// upsampling

MICROJXL__STATIC_RETURNS_ERR microjxl__upsample_frame(microjxl__st *st);

#ifdef MICROJXL_IMPLEMENTATION

/* Default upsampling kernel weights (libjxl image_metadata.cc kWeights2/4/8).
 * Each is the packed upper triangle of the symmetric 5x5 kernel for the
 * top-left output phase of a 2^shift x 2^shift block. */
static const float MICROJXL__UPS2_WEIGHTS[15] = {
	-0.01716200f, -0.03452303f, -0.04022174f, -0.02921014f, -0.00624645f,
	0.14111091f,  0.28896755f,  0.00278718f,  -0.01610267f, 0.56661550f,
	0.03777607f,  -0.01986694f, -0.03144731f, -0.01185068f, -0.00213539f};
static const float MICROJXL__UPS4_WEIGHTS[55] = {
	-0.02419067f, -0.03491987f, -0.03693351f, -0.03094285f, -0.00529785f,
	-0.01663432f, -0.03556863f, -0.03888905f, -0.03516850f, -0.00989469f,
	0.23651958f,  0.33392945f,  -0.01073543f, -0.01313181f, -0.03556694f,
	0.13048175f,  0.40103025f,  0.03951150f,  -0.02077584f, 0.46914198f,
	-0.00209270f, -0.01484589f, -0.04064806f, 0.18942530f,  0.56279892f,
	0.06674400f,  -0.02335494f, -0.03551682f, -0.00754830f, -0.02267919f,
	-0.02363578f, 0.00315804f,  -0.03399098f, -0.01359519f, -0.00091653f,
	-0.00335467f, -0.01163294f, -0.01610294f, -0.00974088f, -0.00191622f,
	-0.01095446f, -0.03198464f, -0.04455121f, -0.02799790f, -0.00645912f,
	0.06390599f,  0.22963888f,  0.00630981f,  -0.01897349f, 0.67537268f,
	0.08483369f,  -0.02534994f, -0.02205197f, -0.01667999f, -0.00384443f};
static const float MICROJXL__UPS8_WEIGHTS[210] = {
	-0.02928613f, -0.03706353f, -0.03783812f, -0.03324558f, -0.00447632f,
	-0.02519406f, -0.03752601f, -0.03901508f, -0.03663285f, -0.00646649f,
	-0.02066407f, -0.03838633f, -0.04002101f, -0.03900035f, -0.00901973f,
	-0.01626393f, -0.03954148f, -0.04046620f, -0.03979621f, -0.01224485f,
	0.29895328f,  0.35757708f,  -0.02447552f, -0.01081748f, -0.04314594f,
	0.23903219f,  0.41119301f,  -0.00573046f, -0.01450239f, -0.04246845f,
	0.17567618f,  0.45220643f,  0.02287757f,  -0.01936783f, -0.03583255f,
	0.11572472f,  0.47416733f,  0.06284440f,  -0.02685066f, 0.42720050f,
	-0.02248939f, -0.01155273f, -0.04562755f, 0.28689496f,  0.49093869f,
	-0.00007891f, -0.01545926f, -0.04562659f, 0.21238920f,  0.53980934f,
	0.03369474f,  -0.02070211f, -0.03866988f, 0.14229550f,  0.56593398f,
	0.08045181f,  -0.02888298f, -0.03680918f, -0.00542229f, -0.02920477f,
	-0.02788574f, -0.02118180f, -0.03942402f, -0.00775547f, -0.02433614f,
	-0.03193943f, -0.02030828f, -0.04044014f, -0.01074016f, -0.01930822f,
	-0.03620399f, -0.01974125f, -0.03919545f, -0.01456093f, -0.00045072f,
	-0.00360110f, -0.01020207f, -0.01231907f, -0.00638988f, -0.00071592f,
	-0.00279122f, -0.00957115f, -0.01288327f, -0.00730937f, -0.00107783f,
	-0.00210156f, -0.00890705f, -0.01317668f, -0.00813895f, -0.00153491f,
	-0.02128481f, -0.04173044f, -0.04831487f, -0.03293190f, -0.00525260f,
	-0.01720322f, -0.04052736f, -0.05045706f, -0.03607317f, -0.00738030f,
	-0.01341764f, -0.03965629f, -0.05151616f, -0.03814886f, -0.01005819f,
	0.18968273f,  0.33063684f,  -0.01300105f, -0.01372950f, -0.04017465f,
	0.13727832f,  0.36402234f,  0.01027890f,  -0.01832107f, -0.03365072f,
	0.08734506f,  0.38194295f,  0.04338228f,  -0.02525993f, 0.56408126f,
	0.00458352f,  -0.01648227f, -0.04887868f, 0.24585519f,  0.62026135f,
	0.04314807f,  -0.02213737f, -0.04158014f, 0.16637289f,  0.65027023f,
	0.09621636f,  -0.03101388f, -0.04082742f, -0.00904519f, -0.02790922f,
	-0.02117818f, 0.00798662f,  -0.03995711f, -0.01243427f, -0.02231705f,
	-0.02946266f, 0.00992055f,  -0.03600283f, -0.01684920f, -0.00111684f,
	-0.00411204f, -0.01297130f, -0.01723725f, -0.01022545f, -0.00165306f,
	-0.00313110f, -0.01218016f, -0.01763266f, -0.01125620f, -0.00231663f,
	-0.01374149f, -0.03797620f, -0.05142937f, -0.03117307f, -0.00581914f,
	-0.01064003f, -0.03608089f, -0.05272168f, -0.03375670f, -0.00795586f,
	0.09628104f,  0.27129991f,  -0.00353779f, -0.01734151f, -0.03153981f,
	0.05686230f,  0.28500998f,  0.02230594f,  -0.02374955f, 0.68214326f,
	0.05018048f,  -0.02320852f, -0.04383616f, 0.18459474f,  0.71517975f,
	0.10805613f,  -0.03263677f, -0.03637639f, -0.01394373f, -0.02511203f,
	-0.01728636f, 0.05407331f,  -0.02867568f, -0.01893131f, -0.00240854f,
	-0.00446511f, -0.01636187f, -0.02377053f, -0.01522848f, -0.00333334f,
	-0.00819975f, -0.02964169f, -0.04499287f, -0.02745350f, -0.00612408f,
	0.02727416f,  0.19446600f,  0.00159832f,  -0.02232473f, 0.74982506f,
	0.11452620f,  -0.03348048f, -0.01605681f, -0.02070339f, -0.00458223f};

/* Build the N*N x 25 kernel table from the packed weights, mirroring the
 * symmetry expansion in libjxl's UpsamplingStage constructor. */
MICROJXL_STATIC void microjxl__build_upsample_kernel(int32_t log_factor, const float *weights, float *kernel) {
	int32_t N = 1 << log_factor, H = N >> 1, ky, kx, py, px;
	for (ky = 0; ky < H; ++ky) {
		for (kx = 0; kx < H; ++kx) {
			int32_t o0 = (ky * N + kx) * 25;
			int32_t o1 = (ky * N + (N - 1 - kx)) * 25;
			int32_t o2 = ((N - 1 - ky) * N + kx) * 25;
			int32_t o3 = ((N - 1 - ky) * N + (N - 1 - kx)) * 25;
			for (py = 0; py < 5; ++py) {
				for (px = 0; px < 5; ++px) {
					int32_t j = 5 * ky + py, i = 5 * kx + px;
					int32_t my = microjxl__min32(i, j), mx = microjxl__max32(i, j);
					float w = weights[5 * H * my - my * (my - 1) / 2 + mx - my];
					kernel[o0 + py * 5 + px] = w;
					kernel[o1 + py * 5 + (4 - px)] = w;
					kernel[o2 + (4 - py) * 5 + px] = w;
					kernel[o3 + (4 - py) * 5 + (4 - px)] = w;
				}
			}
		}
	}
}

/* Convert an integer EC plane to a normalized (0..1) float plane in place
 * (same domain as libjxl's float render-pipeline rows for extra channels).
 * The integer plane is freed; *p becomes the float plane. */
MICROJXL__STATIC_RETURNS_ERR microjxl__ec_to_float_plane(microjxl__st *st, microjxl__plane *p, const microjxl__ec_info *ec) {
	microjxl__plane out = MICROJXL__INIT;
	float scale = (float) microjxl__maxpixel_alpha(ec);
	int32_t y, x;
	MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, p->width, p->height, MICROJXL__PLANE_FORCE_PAD, &out));
	for (y = 0; y < p->height; ++y) {
		const int32_t *s3 = p->type == MICROJXL__PLANE_I32 ? MICROJXL__I32_PIXELS(p, y) : NULL;
		const int16_t *s2 = p->type == MICROJXL__PLANE_I16 ? MICROJXL__I16_PIXELS(p, y) : NULL;
		float *d = MICROJXL__F32_PIXELS(&out, y);
		for (x = 0; x < p->width; ++x) d[x] = s3 ? (float) s3[x] / scale : (float) s2[x] / scale;
	}
	microjxl__mem_free_plane(p);
	*p = out;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_plane(&out);
	return st->err;
}

/* Upsample one plane by 2^log_factor using the 25-tap kernel with the result
 * clamped to the min/max of the 5x5 source neighbourhood (libjxl
 * stage_upsampling.cc). The destination has the frame's final dimensions. */
MICROJXL__STATIC_RETURNS_ERR microjxl__upsample_plane(microjxl__st *st, int32_t log_factor, microjxl__plane *p) {
	microjxl__frame_st *f = st->frame;
	static const float *const DEFAULT_WEIGHTS[4] = { NULL, MICROJXL__UPS2_WEIGHTS, MICROJXL__UPS4_WEIGHTS, MICROJXL__UPS8_WEIGHTS };
	microjxl__image_st *im = st->image;
	const float *weights;
	int32_t N = 1 << log_factor, sw = p->width, sh = p->height;
	int32_t ow = f->upsampled_width, oh = f->upsampled_height;
	int32_t x, y, ox, oy, k;
	microjxl__plane out = MICROJXL__INIT;
	float *kernel = NULL;
	float win[25];

	MICROJXL__ASSERT(1 <= log_factor && log_factor <= 3);
	if (sw == ow && sh == oh) return 0;
	MICROJXL__TRY(microjxl__init_plane(st, p->type, ow, oh, MICROJXL__PLANE_FORCE_PAD, &out));
	MICROJXL__TRY_MALLOC(float, &kernel, (size_t) (N * N * 25));
	/* D.3: a set cw_mask bit overrides the default kernel for that factor */
	weights = DEFAULT_WEIGHTS[log_factor];
	if ((im->up_cw_mask & (1u << (log_factor - 1))) && log_factor == 1) weights = im->up2_weight;
	else if ((im->up_cw_mask & (1u << (log_factor - 1))) && log_factor == 2) weights = im->up4_weight;
	else if ((im->up_cw_mask & (1u << (log_factor - 1))) && log_factor == 3) weights = im->up8_weight;
	microjxl__build_upsample_kernel(log_factor, weights, kernel);

#define MICROJXL__UPSAMPLE_BODY(PIX, pixel_t) \
	for (y = 0; y < sh; ++y) { \
		for (x = 0; x < sw; ++x) { \
			float mn = FLT_MAX, mx = -FLT_MAX; \
			int32_t iy, ix; \
			for (iy = -2; iy <= 2; ++iy) { \
				int32_t yy = microjxl__mirror2(y + iy, sh); \
				const pixel_t *row = MICROJXL__TYPED_PIXELS(p, yy, p->type, pixel_t); \
				for (ix = -2; ix <= 2; ++ix) { \
					int32_t xx = microjxl__mirror2(x + ix, sw); \
					float v = (float) row[xx]; \
					win[(iy + 2) * 5 + (ix + 2)] = v; \
					if (v < mn) mn = v; \
					if (v > mx) mx = v; \
				} \
			} \
			for (oy = 0; oy < N; ++oy) { \
				pixel_t *dst = MICROJXL__TYPED_PIXELS(&out, y * N + oy, out.type, pixel_t); \
				for (ox = 0; ox < N; ++ox) { \
					if (x * N + ox >= ow) break; \
					const float *ker = kernel + (oy * N + ox) * 25; \
					float acc = 0.0f; \
					for (k = 0; k < 25; ++k) acc += win[k] * ker[k]; \
					if (acc < mn) acc = mn; \
					if (acc > mx) acc = mx; \
					dst[x * N + ox] = (pixel_t) (acc >= 0.0f ? acc + 0.5f : acc - 0.5f); \
				} \
			} \
		} \
	}

	if (p->type == MICROJXL__PLANE_I32) {
		MICROJXL__UPSAMPLE_BODY(PIX, int32_t);
	} else if (p->type == MICROJXL__PLANE_I16) {
		MICROJXL__UPSAMPLE_BODY(PIX, int16_t);
	} else if (p->type == MICROJXL__PLANE_F32) {
		/* float planes appear here only as VarDCT reference snapshots
		 * (ref_snap) for frames saved with upsampling; the kernel math is
		 * identical, the rounding differs (no int cast) */
#define MICROJXL__UPSAMPLE_BODY_F(PIX) \
	for (y = 0; y < sh; ++y) { \
		for (x = 0; x < sw; ++x) { \
			float mn = FLT_MAX, mx = -FLT_MAX; \
			int32_t iy, ix; \
			for (iy = -2; iy <= 2; ++iy) { \
				int32_t yy = microjxl__mirror2(y + iy, sh); \
				const float *row = MICROJXL__F32_PIXELS(p, yy); \
				for (ix = -2; ix <= 2; ++ix) { \
					int32_t xx = microjxl__mirror2(x + ix, sw); \
					float v = row[xx]; \
					win[(iy + 2) * 5 + (ix + 2)] = v; \
					if (v < mn) mn = v; \
					if (v > mx) mx = v; \
				} \
			} \
			for (oy = 0; oy < N; ++oy) { \
				float *dst = MICROJXL__F32_PIXELS(&out, y * N + oy); \
				for (ox = 0; ox < N; ++ox) { \
					if (x * N + ox >= ow) break; \
					const float *ker = kernel + (oy * N + ox) * 25; \
					float acc = 0.0f; \
					for (k = 0; k < 25; ++k) acc += win[k] * ker[k]; \
					if (acc < mn) acc = mn; \
					if (acc > mx) acc = mx; \
					dst[x * N + ox] = acc; \
				} \
			} \
		} \
	}
		MICROJXL__UPSAMPLE_BODY_F(PIX);
#undef MICROJXL__UPSAMPLE_BODY_F
	} else {
#ifdef MICROJXL_DEBUG
		fprintf(stderr, "[microjxl] upsample: unsupported plane type=%d (log=%d)\n", (int) p->type, (int) log_factor);
#endif
		MICROJXL__RAISE("uspt");
	}
#undef MICROJXL__UPSAMPLE_BODY

	microjxl__mem_free_plane(p);
	*p = out;
	microjxl__mem_free(kernel);
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_plane(&out);
	microjxl__mem_free(kernel);
	return st->err;
}

MICROJXL__STATIC_RETURNS_ERR microjxl__upsample_frame(microjxl__st *st) {
	microjxl__frame_st *f = st->frame;
	microjxl__image_st *im = st->image;
	int32_t i, color_channels;

	MICROJXL__ASSERT(f->log_upsampling > 0);
	MICROJXL__ASSERT(f->gmodular.num_channels >= 1);
	color_channels = (f->gmodular.num_channels >= 3) ? 3 : 1;
	for (i = 0; i < color_channels; ++i) {
		MICROJXL__TRY(microjxl__upsample_plane(st, f->log_upsampling, &f->gmodular.channel[i]));
	}
	for (; i < f->gmodular.num_channels; ++i) {
		int32_t ec = i - color_channels;
		int32_t log_u = (f->ec_log_upsampling ? f->ec_log_upsampling[ec] : 0) + im->ec_info[ec].dim_shift;
		/* libjxl runs the extra channels through the FLOAT render pipeline,
		 * so the 25-tap upsampling kernel is evaluated on floats and its
		 * output is never quantized back onto the sample grid
		 * (dec_cache.cc UpsamplingStage). Rounding here costs up to
		 * 0.5/maxval per alpha sample — exactly the conformance
		 * 'upsampling' case's error (peak 0.5/255). Carry the integer
		 * alpha EC of VarDCT frames in a float plane from here on: the
		 * render, the EC canvas blend and the f32 accessors all accept
		 * float EC planes. (Float-sample alpha stays on its own path.)
		 * Applies whatever the EC's own upsampling is: the kernel runs on
		 * the carrier either way. */
		if (!f->is_modular &&
		    im->ec_info[ec].type == MICROJXL__EC_ALPHA &&
		    im->ec_info[ec].exp_bits == 0 &&
		    (f->gmodular.channel[i].type == MICROJXL__PLANE_I16 ||
		     f->gmodular.channel[i].type == MICROJXL__PLANE_I32)) {
			MICROJXL__TRY(microjxl__ec_to_float_plane(st, &f->gmodular.channel[i], &im->ec_info[ec]));
		}
		MICROJXL__TRY(microjxl__upsample_plane(st, log_u, &f->gmodular.channel[i]));
	}
	/* VarDCT XYB snapshots (deferred conversion carrier and/or saved XYB
	 * refs) are upsampled too; save_ref_frame records refs at the final
	 * resolution (im->ref_w/h), so the floats must live there. */
	if (f->ref_snap[0].type == MICROJXL__PLANE_F32) {
		for (i = 0; i < 3; ++i) {
			MICROJXL__TRY(microjxl__upsample_plane(st, f->log_upsampling, &f->ref_snap[i]));
		}
	}
	/* all planes are now at the final size; the render reads f->width/height */
	f->width = f->upsampled_width;
	f->height = f->upsampled_height;
	return 0;

MICROJXL__ON_ERROR:
	return st->err;
}

/* VarDCT XYB frames with upsampling: the combine skipped the final write,
 * because libjxl upsamples the float XYB samples BEFORE the nonlinear
 * XYB->RGB stage (dec_cache.cc: UpsamplingStage sits before GetXYBStage;
 * the kernel's negative lobes are not equivalent across the nonlinear
 * conversion). The upsampled correlated-XYB floats live in ref_snap;
 * apply the merge, opsin cube, inverse matrix, HLG OOTF and transfer
 * function here, at final resolution. Output planes are HALF-floats
 * (libjxl's render pipeline rows are float16); the render reads them
 * directly, so no intermediate integer quantization happens. */
MICROJXL__STATIC_RETURNS_ERR microjxl__finalize_xyb_color(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	float cbrt_bias[3], itscale = 255.0f / im->intensity_target;
	int32_t y, x, c;

	for (c = 0; c < 3; ++c) {
		MICROJXL__SHOULD(f->ref_snap[c].type == MICROJXL__PLANE_F32, "fxyb");
		MICROJXL__SHOULD(f->gmodular.channel[c].type == MICROJXL__PLANE_I32, "fxyb");
	}
	for (c = 0; c < 3; ++c) cbrt_bias[c] = cbrtf(im->opsin_bias[c]);
	/* deferred noise: generate+convolve at the final (upsampled) resolution
	 * (add_noise_frame allocates f->width/height planes, which are final
	 * here) and mix per-pixel in the loop below, mirroring libjxl's
	 * post-upsampling noise stages. */
	if (f->has_noise) MICROJXL__TRY(microjxl__add_noise_frame(st));
	for (c = 0; c < 3; ++c) {
		MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width, f->height, MICROJXL__PLANE_FORCE_PAD, &f->render_f16[c]));
	}
	for (y = 0; y < f->height; ++y) {
		for (x = 0; x < f->width; ++x) {
			float px = MICROJXL__F32_PIXELS(&f->ref_snap[0], y)[x];
			float py = MICROJXL__F32_PIXELS(&f->ref_snap[1], y)[x];
			float pb = MICROJXL__F32_PIXELS(&f->ref_snap[2], y)[x];
			float p[3] = { py + px, py - px, pb };
			float mixed[3], ootf;
			/* K.5: noise modulates the XYB samples after upsampling; the
			 * mixed values are written back so ref_snap stays a valid
			 * post-noise snapshot for save_ref_frame. */
			if (f->has_noise) {
				microjxl__apply_noise_xyb(f, f->noise_planes, x, y, &px, &py, &pb);
				MICROJXL__F32_PIXELS(&f->ref_snap[0], y)[x] = px;
				MICROJXL__F32_PIXELS(&f->ref_snap[1], y)[x] = py;
				MICROJXL__F32_PIXELS(&f->ref_snap[2], y)[x] = pb;
				p[0] = py + px; p[1] = py - px; p[2] = pb;
			}
			for (c = 0; c < 3; ++c) {
				float pp = p[c] - cbrt_bias[c];
				mixed[c] = (pp * pp * pp + im->opsin_bias[c]) * itscale;
			}
			ootf = microjxl__hlg_ootf_ratio(im, mixed[0], mixed[1], mixed[2]);
			for (c = 0; c < 3; ++c) {
				float v = mixed[0] * im->opsin_inv_mat[c][0] +
					mixed[1] * im->opsin_inv_mat[c][1] +
					mixed[2] * im->opsin_inv_mat[c][2];
				v *= ootf;
				/* want_icc images carry the transfer function in the ICC
				 * profile; libjxl keeps the pipeline rows linear and lets
				 * the profile describe them (MaybeCreateProfile uses the
				 * pipeline tf only when the stream declares one). */
				if (!im->want_icc) v = microjxl__tf_encode(im, v); // to the image's transfer function
				/* libjxl's pipeline rows keep unclamped display-referred
				 * floats (in half precision); only the u8/u16 output
				 * conversions clamp. Half-floats also cap |v| at 65504. */
				MICROJXL__F32_PIXELS(&f->render_f16[c], y)[x] = v;
			}
		}
	}
	return 0;

MICROJXL__ON_ERROR:
	return st->err;
}

/* VarDCT frame finalization (libjxl render-pipeline stage order for VarDCT,
 * dec_cache.cc): chroma upsampling -> gaborish -> EPF -> patches -> splines
 * -> [upsampling] -> noise -> XYB/YCbCr conversion. All filter stages are
 * FRAME-WIDE (SymmetricBorderOnly), so they run here over the whole image
 * after every LF group has placed its IDCT output into vardct_f — filtering
 * per group would mirror at group seams instead of crossing them.
 *
 * The pre-conversion stage output is ALSO the reference/DC capture domain
 * (libjxl WriteToImageBundleStage / WriteToImage3FStage copy the pipeline
 * rows verbatim): ref_snap keeps the correlated (px, py, pb) floats at the
 * save-before-colour-transform point (post-noise), exactly like before.
 *
 * For log_upsampling == 0 frames the colour conversion produces the final
 * unclamped display-referred float planes (render_f16, kept as float32).
 * For log_upsampling > 0 frames the conversion is deferred to
 * finalize_xyb_color after upsample_frame grows ref_snap (UpsamplingStage
 * precedes the XYB stage). */
MICROJXL__STATIC_RETURNS_ERR microjxl__finalize_vardct_frame(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	microjxl__lf_group_st fgg;
	int32_t w = f->width, h = f->height;
	int32_t x, y, c;
	float cbrt_bias[3], itscale = 255.0f / im->intensity_target;

	/* 1. chroma upsampling (YCbCr subsampled channels only): 0.75/0.25
	 * linear, horizontal then vertical, reflect-101 borders, identical to
	 * libjxl's Horizontal/VerticalChromaUpsamplingStage. */
	for (c = 0; c < 3; ++c) {
		int32_t hshift = f->jpeg_hshift[c], vshift = f->jpeg_vshift[c];
		int32_t ssw, ssh;
		float *ss = f->vardct_ss[c];
		if (!f->do_ycbcr || !ss) continue;
		ssw = f->vardct_ss_w[c]; ssh = f->vardct_ss_h[c];
		/* horizontal into vardct_f at full width (rows are ssh apart) */
		for (y = 0; y < ssh; ++y) {
			const float *src = ss + (size_t) y * ssw;
			float *dst = MICROJXL__F32_PIXELS(&f->vardct_f[c], y);
			for (x = 0; x < ssw; ++x) {
				float curv = src[x];
				float prev = src[microjxl__mirror1d(x - 1, ssw)];
				float next = src[microjxl__mirror1d(x + 1, ssw)];
				dst[2 * x] = 0.75f * curv + 0.25f * prev;
				dst[2 * x + 1] = 0.75f * curv + 0.25f * next;
			}
		}
		/* vertical, in place, bottom-up (reads the horizontally-upsampled rows) */
		for (y = ssh - 1; y >= 0; --y) {
			float *top = MICROJXL__F32_PIXELS(&f->vardct_f[c], microjxl__mirror1d(y - 1, ssh));
			float *mid = MICROJXL__F32_PIXELS(&f->vardct_f[c], y);
			float *bot = MICROJXL__F32_PIXELS(&f->vardct_f[c], microjxl__mirror1d(y + 1, ssh));
			float *dst0 = MICROJXL__F32_PIXELS(&f->vardct_f[c], 2 * y);
			float *dst1 = MICROJXL__F32_PIXELS(&f->vardct_f[c], 2 * y + 1);
			for (x = 0; x < 2 * ssw && x < w; ++x) {
				float curv = mid[x];
				/* dst1 BEFORE dst0: at y=1 bot (row 2) aliases dst0 (row 2y = 2),
				 * so dst0 must not overwrite it before dst1 reads it. (The
				 * previous dst0-first order corrupted exactly output row 3.) */
				dst1[x] = 0.75f * curv + 0.25f * bot[x];
				dst0[x] = 0.75f * curv + 0.25f * top[x];
			}
		}
	}

	/* 2. gaborish + 3. EPF, frame-wide (libjxl SymmetricBorderOnly stages) */
	if (f->gab.enabled || f->epf.iters) {
	if (f->gab.enabled) MICROJXL__TRY(microjxl__gaborish(st, f->vardct_f));
	if (f->epf.iters) {
		memset(&fgg, 0, sizeof(fgg));
		fgg.width = w; fgg.height = h;
		fgg.width8 = microjxl__ceil_div32(w, 8);
		fgg.height8 = microjxl__ceil_div32(h, 8);
		MICROJXL__TRY(microjxl__epf_recip_sigmas(st, &f->epf_sigmas));
		MICROJXL__TRY(microjxl__epf(st, f->vardct_f, &fgg, &f->epf_sigmas));
	}
	}

	/* 4. patches -> 5. splines (pre-upsampling, on the correlated floats).
	 * Pixel-level application at frame scope. */
	if (f->has_patches || f->has_splines) {
		for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
			float px = MICROJXL__F32_PIXELS(&f->vardct_f[0], y)[x];
			float py = MICROJXL__F32_PIXELS(&f->vardct_f[1], y)[x];
			float pb = MICROJXL__F32_PIXELS(&f->vardct_f[2], y)[x];
			if (f->has_patches) {
				float pv[3] = { px, py, pb };
				MICROJXL__TRY(microjxl__apply_patches_pixel(st, x, y, pv));
				px = pv[0]; py = pv[1]; pb = pv[2];
			}
			if (f->has_splines) {
				MICROJXL__TRY(microjxl__apply_splines_xyb(st, f, x, y, &px, &py, &pb));
			}
			MICROJXL__F32_PIXELS(&f->vardct_f[0], y)[x] = px;
			MICROJXL__F32_PIXELS(&f->vardct_f[1], y)[x] = py;
			MICROJXL__F32_PIXELS(&f->vardct_f[2], y)[x] = pb;
		}
	}

	/* 6. noise (stored-resolution generation for non-upsampled frames; the
	 * upsampled path defers to finalize_xyb_color which generates at the
	 * final resolution, matching libjxl's UpsamplingStage -> noise order). */
	if (f->has_noise && (f->do_ycbcr || f->log_upsampling == 0)) {
		MICROJXL__TRY(microjxl__add_noise_frame(st));
		if (f->noise_ready) {
			for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
				float px = MICROJXL__F32_PIXELS(&f->vardct_f[0], y)[x];
				float py = MICROJXL__F32_PIXELS(&f->vardct_f[1], y)[x];
				float pb = MICROJXL__F32_PIXELS(&f->vardct_f[2], y)[x];
				microjxl__apply_noise_xyb(f, f->noise_planes, x, y, &px, &py, &pb);
				MICROJXL__F32_PIXELS(&f->vardct_f[0], y)[x] = px;
				MICROJXL__F32_PIXELS(&f->vardct_f[1], y)[x] = py;
				MICROJXL__F32_PIXELS(&f->vardct_f[2], y)[x] = pb;
			}
		}
	}

	/* 7. capture the save-before-colour-transform point (post-noise, pre-merge).
	 * ref_snap already exists when save_before_ct / LF frame / upsampled XYB. */
	if (f->ref_snap[0].type == MICROJXL__PLANE_F32) {
		for (c = 0; c < 3; ++c) {
			for (y = 0; y < h; ++y) {
				memcpy(MICROJXL__F32_PIXELS(&f->ref_snap[c], y),
					MICROJXL__F32_PIXELS(&f->vardct_f[c], y),
					sizeof(float) * (size_t) w);
			}
		}
	}

	/* 8. colour conversion. For upsampled XYB frames the conversion is
	 * deferred to finalize_xyb_color (which reads ref_snap); the YCbCr
	 * conversion (linear) happens here for both. */
	if (!f->do_ycbcr && f->log_upsampling > 0) {
		/* deferred; vardct_f no longer needed */
		for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&f->vardct_f[c]);
		return 0;
	}

	for (c = 0; c < 3; ++c) cbrt_bias[c] = cbrtf(im->opsin_bias[c]);
	/* The converted floats always land in render_f16 (libjxl's render
	 * pipeline rows are unclamped floats end to end; quantizing to the
	 * integer gmodular planes here would cost ~1/(255*sqrt(12)) rms and
	 * fail the strictest conformance limits). The int write-back below
	 * keeps working for consumers that need it (canvas blending etc.) but
	 * the render reads the float planes. */
	for (c = 0; c < 3; ++c) {
		if (f->render_f16[c].type != MICROJXL__PLANE_F32) {
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, w, h, MICROJXL__PLANE_FORCE_PAD, &f->render_f16[c]));
		}
	}
#ifdef MICROJXL_DEBUG
	float outr0_v[3] = {0, 0, 0};
#endif
	for (y = 0; y < h; ++y) {
		float *in0 = MICROJXL__F32_PIXELS(&f->vardct_f[0], y);
		float *in1 = MICROJXL__F32_PIXELS(&f->vardct_f[1], y);
		float *in2 = MICROJXL__F32_PIXELS(&f->vardct_f[2], y);
		float *outr = NULL, *outg = NULL, *outb = NULL, *outa = NULL;
		int16_t *pix16[3] = {NULL, NULL, NULL};
		int32_t *pix32[3] = {NULL, NULL, NULL};
		outr = MICROJXL__F32_PIXELS(&f->render_f16[0], y);
		outg = MICROJXL__F32_PIXELS(&f->render_f16[1], y);
		outb = MICROJXL__F32_PIXELS(&f->render_f16[2], y);
		if (f->do_ycbcr) {
			if (f->gmodular.channel[0].type == MICROJXL__PLANE_I16) {
				for (c = 0; c < 3; ++c) pix16[c] = MICROJXL__I16_PIXELS(&f->gmodular.channel[c], y);
			} else if (f->gmodular.channel[0].type == MICROJXL__PLANE_I32) {
				for (c = 0; c < 3; ++c) pix32[c] = MICROJXL__I32_PIXELS(&f->gmodular.channel[c], y);
			}
		}
		for (x = 0; x < w; ++x) {
			float v[3];
			if (f->do_ycbcr) {
				/* YCbCr -> RGB: full-range BT.601 as defined by JFIF (T.871).
				 * samples are centered: Y is offset by 128/255, Cb/Cr already. */
				float yy = in1[x] + 128.0f / 255.0f;
				float cb = in0[x], cr = in2[x];
				v[0] = yy + 1.402f * cr;
				v[1] = yy - 0.114f * 1.772f / 0.587f * cb - 0.299f * 1.402f / 0.587f * cr;
				v[2] = yy + 1.772f * cb;
				/* the float rows always get the unquantized values (libjxl
				 * pipeline rows); the int write-back below is for consumers
				 * that read the gmodular planes */
				outr[x] = v[0]; outg[x] = v[1]; outb[x] = v[2];
				if (pix16[0]) {
					for (c = 0; c < 3; ++c) {
						float q = v[c]; if (q < 0.0f) q = 0.0f; else if (q > 1.0f) q = 1.0f;
						pix16[c][x] = (int16_t) ((float) microjxl__maxpixel_scale(im) * q + 0.5f);
					}
				} else if (pix32[0]) {
					for (c = 0; c < 3; ++c) {
						float q = v[c]; if (q < 0.0f) q = 0.0f; else if (q > 1.0f) q = 1.0f;
						pix32[c][x] = (int32_t) ((float) microjxl__maxpixel_scale(im) * q + 0.5f);
					}
				}
			} else {
				/* XYB inverse (identical to finalize_xyb_color): luma mixing,
				 * opsin cube, inverse matrix, HLG OOTF, transfer function. The
				 * combine/filters produce correlated (X, Y, B) floats. */
				float p[3] = { in1[x] + in0[x], in1[x] - in0[x], in2[x] };
				float mixed[3], ootf;
				for (c = 0; c < 3; ++c) {
					float pp = p[c] - cbrt_bias[c];
					mixed[c] = (pp * pp * pp + im->opsin_bias[c]) * itscale;
				}
				ootf = microjxl__hlg_ootf_ratio(im, mixed[0], mixed[1], mixed[2]);
				for (c = 0; c < 3; ++c) {
					v[c] = mixed[0] * im->opsin_inv_mat[c][0] +
						mixed[1] * im->opsin_inv_mat[c][1] +
						mixed[2] * im->opsin_inv_mat[c][2];
					v[c] *= ootf;
					/* want_icc: rows stay linear; the ICC carries the TF */
					if (!im->want_icc) v[c] = microjxl__tf_encode(im, v[c]);
				}
				outr[x] = v[0]; outg[x] = v[1]; outb[x] = v[2];
#ifdef MICROJXL_DEBUG
				if (getenv("MICROJXL_TRACE_FV") && y == 0 && x == 0) {
					outr0_v[0] = v[0]; outr0_v[1] = v[1]; outr0_v[2] = v[2];
				}
#endif
			}
		}
	}
#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_FV")) {
		fprintf(stderr, "[fv] w=%d h=%d ycbcr=%d ups=%d in(0,0)=%g %g %g out(0,0)=%g %g %g\n",
			w, h, f->do_ycbcr, f->log_upsampling,
			(double) MICROJXL__F32_PIXELS(&f->vardct_f[0], 0)[0],
			(double) MICROJXL__F32_PIXELS(&f->vardct_f[1], 0)[0],
			(double) MICROJXL__F32_PIXELS(&f->vardct_f[2], 0)[0],
			(double) outr0_v[0], (double) outr0_v[1], (double) outr0_v[2]);
	}
#endif
	/* the int colour planes stay zeroed for the float path; the render uses
	 * render_f16. Everything below is per-frame, so free the float pipeline. */
	for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&f->vardct_f[c]);
	(void) itscale; (void) cbrt_bias;
	return 0;

MICROJXL__ON_ERROR:
	return st->err;
}

/* Modular frame finalization at STORED resolution (libjxl stage order,
 * dec_cache.cc): gaborish -> EPF -> patches -> splines all run on the
 * float pipeline rows BEFORE upsampling; only noise comes later. The
 * float rows are the normalised sample values (int / (2^bpp - 1),
 * dec_modular.cc factor) or, for XYB frames, the correlated XYB floats
 * rebuilt with m_lf_scaled (the same conversion the render uses, folding
 * libjxl's DCQuants factors). Filtered/splined values are written back
 * into the integer gmodular channels (round to nearest, like
 * apply_patches_modular) so the render sees them unchanged. */
MICROJXL__STATIC_RETURNS_ERR microjxl__finalize_modular_frame(microjxl__st *st) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t color_channels, is_xyb, y, x, c;
	microjxl__plane fpl[3] = MICROJXL__INIT;
	microjxl__lf_group_st fgg;

	/* float (bit-pattern) frames keep the render-time spline path: their
	 * gmodular channels hold raw float bit patterns, so the int write-back
	 * below would corrupt them (rare: lossy float modular). */
	if (im->exp_bits != 0) return 0;
	/* patches were blended into gmodular by apply_patches_modular in the
	 * frame loop (same pre-upsampling point as libjxl's patch stage) */
	if (f->patches.num_pos != 0) return 0;
	if (!f->gab.enabled && f->epf.iters == 0 && !(f->has_splines && f->num_segments > 0)) return 0;

	color_channels = (f->gmodular.num_channels >= 3) ? 3 : 1;
	is_xyb = im->xyb_encoded && color_channels == 3;
	memset(&fgg, 0, sizeof(fgg));
	fgg.width = f->width;
	fgg.height = f->height;
	fgg.width8 = microjxl__ceil_div32(f->width, 8);
	fgg.height8 = microjxl__ceil_div32(f->height, 8);
	for (c = 0; c < 3; ++c) {
		MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width, f->height, 0, &fpl[c]));
	}
	for (y = 0; y < f->height; ++y) {
		for (x = 0; x < f->width; ++x) {
			if (is_xyb) {
				int32_t chv[3], ci;
				for (ci = 0; ci < 3; ++ci) {
					microjxl__plane *cp = &f->gmodular.channel[ci];
					chv[ci] = cp->type == MICROJXL__PLANE_I32 ?
						MICROJXL__I32_PIXELS(cp, y)[x] : MICROJXL__I16_PIXELS(cp, y)[x];
				}
				/* XYB stored as YX(B-Y): correlated X = ch1*m0, Y = ch0*m1,
				 * B = (ch2+ch0)*m2 (the render's verified conversion) */
				MICROJXL__F32_PIXELS(&fpl[0], y)[x] = (float) chv[1] * f->m_lf_scaled[0];
				MICROJXL__F32_PIXELS(&fpl[1], y)[x] = (float) chv[0] * f->m_lf_scaled[1];
				MICROJXL__F32_PIXELS(&fpl[2], y)[x] = (float) ((int64_t) chv[2] + chv[0]) * f->m_lf_scaled[2];
			} else {
				int32_t ci, v;
				float maxf = (float) microjxl__maxpixel_scale(im);
				for (ci = 0; ci < 3; ++ci) {
					microjxl__plane *cp = &f->gmodular.channel[ci < color_channels ? ci : 0];
					v = cp->type == MICROJXL__PLANE_I32 ?
						MICROJXL__I32_PIXELS(cp, y)[x] : MICROJXL__I16_PIXELS(cp, y)[x];
					MICROJXL__F32_PIXELS(&fpl[ci], y)[x] = (float) v / maxf;
				}
			}
		}
	}
	/* restoration filters (libjxl adds gaborish for modular frames too;
	 * EPF uses the constant sigma_for_modular — microjxl__epf selects
	 * that mode when recip_sigmas is NULL) */
	if (f->gab.enabled) MICROJXL__TRY(microjxl__gaborish(st, fpl));
	if (f->epf.iters) MICROJXL__TRY(microjxl__epf(st, fpl, &fgg, NULL));
	/* splines draw at stored-resolution coordinates (the draw cache is
	 * built for upsampled_height in libjxl, but segments outside the
	 * frame are clipped by the span/population bounds, making the cache
	 * equivalent over [0, height)) */
	if (f->has_splines && f->num_segments > 0) {
		for (y = 0; y < f->height; ++y) {
			if (y >= f->spline_cache_h ||
				f->segment_y_start[y] == f->segment_y_start[y + 1]) continue;
			float *r0 = MICROJXL__F32_PIXELS(&fpl[0], y);
			float *r1 = MICROJXL__F32_PIXELS(&fpl[1], y);
			float *r2 = MICROJXL__F32_PIXELS(&fpl[2], y);
			for (x = 0; x < f->width; ++x) {
				float d[3];
				microjxl__spline_deltas_at(f, x, y, d);
				r0[x] += d[0]; r1[x] += d[1]; r2[x] += d[2];
			}
		}
	}
	if (is_xyb) {
		/* XYB: write the filtered floats back into the integer channels
		 * (inverse of the read conversion: X' = v/m0, Y' = v/m1,
		 * B' = v/m2 - Y', restoring the YX(B-Y) layout). The render
		 * re-derives XYB from these and quantization here matches the
		 * encoded-domain precision. */
		for (y = 0; y < f->height; ++y) {
			for (c = 0; c < 3; ++c) {
				microjxl__plane *cp = &f->gmodular.channel[c];
				const float *src = MICROJXL__F32_PIXELS(&fpl[c], y);
				for (x = 0; x < f->width; ++x) {
					float fx = src[x] / f->m_lf_scaled[0];
					float fy = 0.0f, fb = 0.0f;
					int32_t q;
					if (c == 0) {
						q = (int32_t) (fx >= 0.0f ? fx + 0.5f : fx - 0.5f);
					} else if (c == 1) {
						fy = src[x] / f->m_lf_scaled[1];
						q = (int32_t) (fy >= 0.0f ? fy + 0.5f : fy - 0.5f);
					} else {
						fy = (float) MICROJXL__I32_PIXELS(&f->gmodular.channel[0], y)[x];
						if (f->gmodular.channel[0].type != MICROJXL__PLANE_I32)
							fy = (float) MICROJXL__I16_PIXELS(&f->gmodular.channel[0], y)[x];
						fb = src[x] / f->m_lf_scaled[2] - fy;
						q = (int32_t) (fb >= 0.0f ? fb + 0.5f : fb - 0.5f);
					}
					if (cp->type == MICROJXL__PLANE_I32) MICROJXL__I32_PIXELS(cp, y)[x] = q;
					else MICROJXL__I16_PIXELS(cp, y)[x] = (int16_t) q;
				}
			}
		}
	} else {
		/* Non-XYB: keep the filtered floats (int/(2^bpp-1) domain) in the
		 * frame-wide modular_f planes; the render reads them directly
		 * (libjxl keeps float pipeline rows end to end — quantizing here
		 * would collapse the EPF/gaborish output back onto the integer
		 * sample grid). Gray keeps a single luma plane (fpl[0]); the
		 * render replicates it. ECs (alpha) stay in their int channels. */
		for (c = 0; c < color_channels; ++c) {
			microjxl__mem_free_plane(&f->modular_f[c]);
			f->modular_f[c] = fpl[c];
			fpl[c].type = 0; fpl[c].pixels = (uintptr_t) 0; fpl[c].stride_bytes = 0;
		}
		f->modular_f_valid = 1;
		/* moved entries are zeroed above, so the blanket free below only
		 * releases the un-moved planes (gray: fpl[1..2]) */
	}
	for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&fpl[c]);
	return 0;

MICROJXL__ON_ERROR:
	for (c = 0; c < 3; ++c) microjxl__mem_free_plane(&fpl[c]);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// rendering (currently very limited)

MICROJXL__STATIC_RETURNS_ERR microjxl__render_rgba_f32(microjxl__st *st, microjxl__plane *out);

#ifdef MICROJXL_IMPLEMENTATION

/* K.5.2 frame blending (libjxl blending.cc NeedsBlending): a REGULAR or
 * SKIPPROG frame needs canvas compositing when it is not a full-canvas
 * REPLACE. When it does, the background is reference_frames[blend_info
 * .src_ref_frame] (canvas-sized, display domain — libjxl saves references
 * post-blending; a missing slot blends over zeroes). */
static int microjxl__frame_needs_blending(const microjxl__frame_st *f, int32_t num_ec, int32_t canvas_w, int32_t canvas_h) {
	int32_t i;
	if (f->type != MICROJXL__FRAME_REGULAR && f->type != MICROJXL__FRAME_REGULAR_SKIPPROG) return 0;
	if (f->x0 != 0 || f->y0 != 0 || f->width != canvas_w || f->height != canvas_h) return 1; // custom_size_or_origin
	if (f->blend_info.mode != MICROJXL__BLEND_REPLACE) return 1;
	for (i = 0; i < num_ec; ++i) {
		if (f->ec_blend_info[i].mode != MICROJXL__BLEND_REPLACE) return 1;
	}
	return 0;
}

/* Blend one RGBA float row segment: fg from the freshly rendered frame,
 * bg from the referenced canvas. Channel 3 is alpha. The modes mirror
 * libjxl's PerformBlending (blending.cc) with the BlendMode ->
 * PatchBlendMode mapping from stage_blending.cc's make_blending:
 * 0 kReplace -> kReplace (fg)
 * 1 kAdd -> kAdd (bg + fg)
 * 2 kBlend -> kBlendAbove (fg over bg by fg alpha; plain copy without alpha)
 * 3 kAlphaWeightedAdd -> kAlphaWeightedAddAbove (bg + fg * fg alpha;
 *   bg + fg without alpha)
 * 4 kMul -> kMul (bg * fg, optionally clamped)
 * Mode 5 (kNone) would not reach us: NeedsBlending() only fires when at
 * least one channel mode differs from REPLACE, but for channels left at
 * REPLACE we still copy fg. */
#define MICROJXL__CLAMP01F(v) ((v) < 0.0f ? 0.0f : (v) > 1.0f ? 1.0f : (v))

/* Blend one channel of one pixel row segment for the frame-canvas
 * compositing (libjxl PerformBlending, single-channel parts). `chan` is
 * the packed RGBA channel index: -1 selects the three colour channels
 * (0..2) blended together as libjxl's colour blend does; 3+i blends EC i
 * (chan 3..3+num_ec-1). `alpha_c` is the EC index of the alpha channel
 * (-1 when the image has none); `col_alpha_c` is the alpha the colour
 * blend uses (the colour blend's alpha_channel EC), and `premul` says
 * whether that alpha is associated (premultiplied). All planes are packed
 * RGBA; n is the number of pixels. */
static void microjxl__frame_blend_row(
	microjxl__st *st, const microjxl__frame_st *f,
	const float *bg, const float *fg, float *out, int32_t n,
	int32_t chan, int32_t mode, int clamp,
	int32_t alpha_c, int premul, int32_t col_alpha_c,
	const float *fg_alpha_row, const float *bg_alpha_row
) {
	int32_t x;
	(void) st; (void) f;
#define MICROJXL__FBPIX(c) ((bg ? bg[(size_t) x * 4 + (c)] : 0.0f))
#define MICROJXL__FBFG(c) (fg[(size_t) x * 4 + (c)])
#define MICROJXL__FBOUT(c) (out[(size_t) x * 4 + (c)])
/* The packed 4-float planes only carry EC0's sample in channel 3; the
 * frame's alpha-EC plane (fg) and canvas alpha-EC plane (bg) supply the
 * blend alphas separately. `slot` is the EC index the blend uses. */
#define MICROJXL__FBALPHA(slot, is_fg) \
	(is_fg ? (slot >= 0 ? (fg_alpha_row ? fg_alpha_row[x] : MICROJXL__FBFG(3 + slot)) : 1.0f) \
	       : (slot >= 0 ? (bg_alpha_row ? bg_alpha_row[x] : (bg ? MICROJXL__FBPIX(3 + slot) : 0.0f)) : 1.0f))
#define MICROJXL__FBCLAMP(v) ((clamp) ? MICROJXL__CLAMP01F(v) : (v))
	if (chan == -1) {
		/* colour channels, libjxl's colour switch */
		switch (mode) {
		case 0: // kReplace: copy fg
			for (x = 0; x < n; ++x) {
				MICROJXL__FBOUT(0) = MICROJXL__FBFG(0);
				MICROJXL__FBOUT(1) = MICROJXL__FBFG(1);
				MICROJXL__FBOUT(2) = MICROJXL__FBFG(2);
			}
			break;
		case 1: // kAdd
			for (x = 0; x < n; ++x) {
				MICROJXL__FBOUT(0) = MICROJXL__FBPIX(0) + MICROJXL__FBFG(0);
				MICROJXL__FBOUT(1) = MICROJXL__FBPIX(1) + MICROJXL__FBFG(1);
				MICROJXL__FBOUT(2) = MICROJXL__FBPIX(2) + MICROJXL__FBFG(2);
			}
			break;
		case 2: // kBlend: fg over bg by fg alpha (kBlendAbove)
		case 3: // kAlphaWeightedAdd (kAlphaWeightedAddAbove)
			if (alpha_c < 0) {
				/* libjxl: kBlendAbove without alpha copies fg;
				 * kAlphaWeightedAddAbove without alpha is bg + fg */
				if (mode == 2) {
					for (x = 0; x < n; ++x) {
						MICROJXL__FBOUT(0) = MICROJXL__FBFG(0);
						MICROJXL__FBOUT(1) = MICROJXL__FBFG(1);
						MICROJXL__FBOUT(2) = MICROJXL__FBFG(2);
					}
				} else {
					for (x = 0; x < n; ++x) {
						MICROJXL__FBOUT(0) = MICROJXL__FBPIX(0) + MICROJXL__FBFG(0);
						MICROJXL__FBOUT(1) = MICROJXL__FBPIX(1) + MICROJXL__FBFG(1);
						MICROJXL__FBOUT(2) = MICROJXL__FBPIX(2) + MICROJXL__FBFG(2);
					}
				}
				break;
			}
			if (mode == 2) {
				/* PerformAlphaBlending (unassociated) / premultiplied path */
				for (x = 0; x < n; ++x) {
					float fa = MICROJXL__FBCLAMP(MICROJXL__FBALPHA(col_alpha_c, 1));
					float ba = bg ? MICROJXL__FBALPHA(col_alpha_c, 0) : 0.0f;
					int c;
					if (premul) {
						for (c = 0; c < 3; ++c) MICROJXL__FBOUT(c) = MICROJXL__FBFG(c) + MICROJXL__FBPIX(c) * (1.0f - fa);
						/* libjxl's blend_weighted writes the blended alpha to the
						 * alpha EC's row (tmp.Row(3 + alpha)); in the packed plane
						 * that is channel 3, which only exists for alpha_c == 0. */
						if (chan == -1 && col_alpha_c == 0)
							MICROJXL__FBOUT(3) = 1.0f - (1.0f - fa) * (1.0f - ba);
					} else {
						float na = 1.0f - (1.0f - fa) * (1.0f - ba);
						float rna = na > 0.0f ? 1.0f / na : 0.0f;
						for (c = 0; c < 3; ++c) {
							MICROJXL__FBOUT(c) = (MICROJXL__FBFG(c) * fa + MICROJXL__FBPIX(c) * ba * (1.0f - fa)) * rna;
						}
						if (chan == -1 && col_alpha_c == 0) MICROJXL__FBOUT(3) = na;
					}
				}
			} else {
				/* PerformAlphaWeightedAdd: bg + fg * fg_alpha */
				for (x = 0; x < n; ++x) {
					float w = MICROJXL__FBCLAMP(MICROJXL__FBALPHA(col_alpha_c, 1));
					int c;
					for (c = 0; c < 3; ++c) MICROJXL__FBOUT(c) = MICROJXL__FBPIX(c) + MICROJXL__FBFG(c) * w;
				}
			}
			break;
		case 4: // kMul: bg * fg (optionally clamped)
			for (x = 0; x < n; ++x) {
				MICROJXL__FBOUT(0) = MICROJXL__FBPIX(0) * (clamp ? MICROJXL__CLAMP01F(MICROJXL__FBFG(0)) : MICROJXL__FBFG(0));
				MICROJXL__FBOUT(1) = MICROJXL__FBPIX(1) * (clamp ? MICROJXL__CLAMP01F(MICROJXL__FBFG(1)) : MICROJXL__FBFG(1));
				MICROJXL__FBOUT(2) = MICROJXL__FBPIX(2) * (clamp ? MICROJXL__CLAMP01F(MICROJXL__FBFG(2)) : MICROJXL__FBFG(2));
			}
			break;
		default: // >= 5 (kNone / kBlendBelow / kAlphaWeightedAddBelow): keep bg
			for (x = 0; x < n; ++x) {
				MICROJXL__FBOUT(0) = MICROJXL__FBPIX(0);
				MICROJXL__FBOUT(1) = MICROJXL__FBPIX(1);
				MICROJXL__FBOUT(2) = MICROJXL__FBPIX(2);
			}
			break;
		}
	} else {
		/* one extra channel (chan = 3 + ec); libjxl's EC switch */
		int32_t ec = chan - 3;
		int32_t ecmode = mode;
		/* EC blend alpha: the colour blend's alpha channel (libjxl uses
		 * ec_blending[i].alpha_channel for ECs, which the bitstream parses
		 * per EC; microjxl stores it in ec_blend_info[i].alpha_chan) */
		int32_t ealpha = f->ec_blend_info[ec].alpha_chan;
		(void) ecmode;
		switch (mode) {
		case 0: // kReplace: copy fg
			for (x = 0; x < n; ++x) MICROJXL__FBOUT(chan) = MICROJXL__FBFG(chan);
			break;
		case 1: // kAdd
			for (x = 0; x < n; ++x) MICROJXL__FBOUT(chan) = MICROJXL__FBPIX(chan) + MICROJXL__FBFG(chan);
			break;
		case 2: // kBlendAbove: fg over bg by this EC blend's alpha
			for (x = 0; x < n; ++x) {
				float fa = MICROJXL__FBCLAMP(MICROJXL__FBALPHA(ealpha, 1));
				float ba = bg ? MICROJXL__FBPIX(3 + ealpha) : 0.0f;
				if (premul) {
					MICROJXL__FBOUT(chan) = MICROJXL__FBFG(chan) + MICROJXL__FBPIX(chan) * (1.0f - fa);
				} else {
					float na = 1.0f - (1.0f - fa) * (1.0f - ba);
					float rna = na > 0.0f ? 1.0f / na : 0.0f;
					MICROJXL__FBOUT(chan) = (MICROJXL__FBFG(chan) * fa + MICROJXL__FBPIX(chan) * ba * (1.0f - fa)) * rna;
				}
				if (ealpha == ec) {
					/* libjxl's blend_weighted writes the output alpha too
					 * (PerformAlphaBlending with the alpha plane as output) */
					MICROJXL__FBOUT(chan) = 1.0f - (1.0f - fa) * (1.0f - ba);
				}
			}
			break;
		case 3: // kAlphaWeightedAddAbove: bg + fg * fg alpha (this EC's alpha)
			for (x = 0; x < n; ++x) {
				float w = MICROJXL__FBCLAMP(MICROJXL__FBALPHA(ealpha, 1));
				MICROJXL__FBOUT(chan) = MICROJXL__FBPIX(chan) + MICROJXL__FBFG(chan) * w;
			}
			break;
		case 4: // kMul
			for (x = 0; x < n; ++x) {
				MICROJXL__FBOUT(chan) = MICROJXL__FBPIX(chan) * (clamp ? MICROJXL__CLAMP01F(MICROJXL__FBFG(chan)) : MICROJXL__FBFG(chan));
			}
			break;
		default: // >= 5: keep bg
			for (x = 0; x < n; ++x) MICROJXL__FBOUT(chan) = MICROJXL__FBPIX(chan);
			break;
		}
	}
#undef MICROJXL__FBPIX
#undef MICROJXL__FBFG
#undef MICROJXL__FBOUT
#undef MICROJXL__FBALPHA
#undef MICROJXL__FBCLAMP
}

/* K.5.2 frame blending (libjxl stage_blending.cc GetBlendingStage +
 * blending.cc PerformBlending). A non-full-canvas or non-REPLACE frame is
 * composited over the referenced frame's RGBA render (reference_frames
 * [blend_info.src_ref_frame]; a missing slot blends over zeroes). libjxl
 * saves references post-blending, so the referenced render is canvas-sized
 * and in display domain — exactly what im->ref_render holds (seeded by
 * next_frame). The frame's own render (frame-sized) is placed at the crop
 * origin; the region outside the frame rect is copied from the background
 * (libjxl ProcessPaddingRow), and the frame region is blended per channel
 * with the frame's blend modes (colour via blend_info, EC i via
 * ec_blend_info[i]; libjxl blends ECs before the colour channels so the
 * colour blend sees the pre-blend alpha). */
MICROJXL__STATIC_RETURNS_ERR microjxl__frame_blend_canvas(
	microjxl__st *st, const microjxl__plane *fg_rgba, microjxl__plane *out,
	microjxl__plane **fg_ec, int32_t n_fg_ec, const microjxl__plane *bg_ec_canvas,
	const microjxl__plane *ec_alpha_pre
) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t w = im->width, h = im->height;
	int32_t num_ec = im->num_extra_channels;
	int32_t y, i, x0c, x1c;
	int has_alpha = 0, alpha_c = 0, premul = 0, col_alpha;
	/* blend background = reference_frames[blend_info.src_ref_frame]
	 * (libjxl stage_blending: bg_ comes from the source slot; an absent
	 * slot blends over zeroes). Slots hold the post-blend DISPLAY render
	 * of each visible referencable frame as interleaved RGBA ([0,1]
	 * floats, alpha EC in channel 3) — see microjxl__store_blend_ref. */
	int32_t bslot = f->blend_info.src_ref_frame;
	const microjxl__plane *bgc = NULL;
	if (bslot >= 0 && bslot < 4 && im->ref_rgba[bslot].type == MICROJXL__PLANE_F32 &&
	    im->ref_rgba[bslot].width >= w * 4 && im->ref_rgba[bslot].height >= h) {
		bgc = &im->ref_rgba[bslot];
	}

	for (i = 0; i < num_ec; ++i) {
		if (im->ec_info[i].type == MICROJXL__EC_ALPHA) { has_alpha = 1; alpha_c = i; break; }
	}
	if (!has_alpha) {
		col_alpha = -1;
	} else {
		/* the colour blend's alpha_channel indexes the EC list (libjxl
		 * extra_channel_info[alpha]); only a real alpha channel carries the
		 * associated flag, so reject anything else */
		col_alpha = f->blend_info.alpha_chan;
		MICROJXL__SHOULD(col_alpha >= 0 && col_alpha < num_ec &&
			im->ec_info[col_alpha].type == MICROJXL__EC_ALPHA, "TODO");
		premul = im->ec_info[col_alpha].data.alpha_associated;
	}

	MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, w * 4, h, MICROJXL__PLANE_FORCE_PAD, out));
	if (!has_alpha) {
		/* no alpha channel in the image: the displayed canvas is opaque
		 * (libjxl's output conversion defaults missing alpha to 1.0; the
		 * blend loop below only writes RGB when num_ec == 0) */
		for (y = 0; y < h; ++y) {
			float *outrow = MICROJXL__F32_PIXELS(out, y);
			for (i = 0; i < w; ++i) outrow[(size_t) i * 4 + 3] = 1.0f;
		}
	}
	for (y = 0; y < h; ++y) {
		const float *bgrow = bgc ? MICROJXL__F32_PIXELS(bgc, y) : NULL;
		float *outrow = MICROJXL__F32_PIXELS(out, y);
		int32_t fy = y - f->y0; // row within the frame render
		if (fy < 0 || fy >= f->height) {
			/* outside the frame rect: padding rows keep the background;
			 * with no background they are ZEROED (libjxl ProcessPaddingRow
			 * zeroes blocks the frame does not cover when there is no
			 * preceding frame to blend over) */
			if (bgrow) memcpy(outrow, bgrow, (size_t) w * 4 * sizeof(float));
			else memset(outrow, 0, (size_t) w * 4 * sizeof(float));
			continue;
		}
		/* x ranges of the frame rect clipped to the canvas */
		x0c = f->x0 < 0 ? 0 : f->x0;
		x1c = f->x0 + f->width > w ? w : f->x0 + f->width;
		/* left / right of the frame (background; zeroed when absent) */
		if (x0c > 0) {
			if (bgrow) memcpy(outrow, bgrow, (size_t) x0c * 4 * sizeof(float));
			else memset(outrow, 0, (size_t) x0c * 4 * sizeof(float));
		}
		if (x1c < w) {
			if (bgrow) memcpy(outrow + (size_t) x1c * 4, bgrow + (size_t) x1c * 4, (size_t) (w - x1c) * 4 * sizeof(float));
			else memset(outrow + (size_t) x1c * 4, 0, (size_t) (w - x1c) * 4 * sizeof(float));
		}
		/* frame region: per-channel blend (libjxl PerformBlending) */
		{
			const float *bgr = bgrow ? bgrow + (size_t) x0c * 4 : NULL;
			const float *fgr = MICROJXL__F32_PIXELS(fg_rgba, fy) + (size_t) (x0c - f->x0) * 4;
			float *outr = outrow + (size_t) x0c * 4;
			int32_t n = x1c - x0c;
			if (n <= 0) continue;
		/* Blend alphas: the frame's alpha-EC plane (fg) and the canvas
		 * alpha-EC plane (bg), NOT packed channel 3+slot — the packed
		 * 4-float plane only carries EC0 in channel 3, so a col_alpha_c
		 * > 0 would wrap into the next pixel's colour channels. Frame EC
		 * planes are raw integer samples: normalize by the EC's maxval. */
		const float *fg_a_row = NULL, *bg_a_row = NULL;
		float fg_a_buf[4096];
		if (has_alpha && alpha_c != 0 && fy >= 0 && fy < f->height) {
			const microjxl__plane *ap = (fg_ec && alpha_c < n_fg_ec) ? fg_ec[alpha_c] : NULL;
			if (ap && ap->width >= f->width && ap->height > fy &&
			    (ap->type == MICROJXL__PLANE_F32 || ap->type == MICROJXL__PLANE_I16 ||
			     ap->type == MICROJXL__PLANE_I32)) {
				float ascale = (float) microjxl__maxpixel_alpha(&im->ec_info[alpha_c]);
				int32_t xa = x0c - f->x0, xb = x1c - f->x0, xx;
				if (n <= (int32_t) (sizeof(fg_a_buf) / sizeof(fg_a_buf[0]))) {
					for (xx = 0; xx < n; ++xx) {
						int32_t sx = xa + xx;
						if (sx < 0 || sx >= xb) { fg_a_buf[xx] = 0.0f; continue; }
						if (ap->type == MICROJXL__PLANE_F32)
							fg_a_buf[xx] = MICROJXL__F32_PIXELS(ap, fy)[sx];
						else if (ap->type == MICROJXL__PLANE_I32)
							fg_a_buf[xx] = (float) MICROJXL__I32_PIXELS(ap, fy)[sx] / ascale;
						else
							fg_a_buf[xx] = (float) MICROJXL__I16_PIXELS(ap, fy)[sx] / ascale;
					}
					fg_a_row = fg_a_buf;
				}
			}
		}
		if (has_alpha && alpha_c < num_ec) {
			/* The colour/EC blends must read the PRE-FRAME canvas alpha
			 * (libjxl PerformBlending reads bg_ rows untouched; its EC blend
			 * runs first precisely so the colour blend sees the pre-blending
			 * alpha). The EC canvas was already updated for THIS frame by
			 * frame_ec_blend_canvas, so use the snapshot taken before it
			 * (`ec_alpha_pre`; NULL = no canvas existed before this frame). */
			if (ec_alpha_pre && ec_alpha_pre->type == MICROJXL__PLANE_F32) {
				bg_a_row = MICROJXL__F32_PIXELS(ec_alpha_pre, y) + x0c;
			} else if (bg_ec_canvas && bg_ec_canvas[alpha_c].type == MICROJXL__PLANE_F32) {
				bg_a_row = MICROJXL__F32_PIXELS(&bg_ec_canvas[alpha_c], y) + x0c;
			}
		}
		{ static int32_t tx = -1, ty = -1; static int frame_ct = 0;
		  int at_target = 0; int32_t sx_t = 0;
		  if (tx == -1) { const char *tp = getenv("MICROJXL_TRACE_BLEND_AT");
		    if (tp) sscanf(tp, "%d,%d", &tx, &ty); else tx = -2; }
		  if (tx >= 0 && y == ty && tx >= f->x0 && tx < f->x0 + f->width) {
		    at_target = 1; sx_t = tx - x0c;
		    fprintf(stderr, "[mj-bld] f=%d fa=%g ba=%g mode=%d premul=%d fg=%g,%g,%g bg=%g,%g,%g\n",
		        frame_ct, fg_a_row ? fg_a_row[sx_t] : -1.0f,
		        bg_a_row ? bg_a_row[sx_t] : -1.0f, f->blend_info.mode, premul,
		        fgr[sx_t*4+0], fgr[sx_t*4+1], fgr[sx_t*4+2],
		        bgr ? bgr[sx_t*4+0] : -1.0f, bgr ? bgr[sx_t*4+1] : -1.0f, bgr ? bgr[sx_t*4+2] : -1.0f);
		  }
			/* ECs first (so the colour blend still sees the pre-blend alpha),
			 * then the colour channels. Only the ALPHA EC (i == 0, chan == 3)
			 * has a slot in the 4-float RGBA plane; higher ECs would wrap into
			 * the next pixel's colour channels — they are composited into the
			 * EC canvas instead (microjxl__frame_ec_blend_canvas). */
			if (num_ec > 0) {
				microjxl__frame_blend_row(st, f, bgr, fgr, outr, n, 3,
					f->ec_blend_info[0].mode, f->ec_blend_info[0].clamp,
					has_alpha ? alpha_c : -1, premul, col_alpha, fg_a_row, bg_a_row);
			}
			microjxl__frame_blend_row(st, f, bgr, fgr, outr, n, -1,
				f->blend_info.mode, f->blend_info.clamp,
				has_alpha ? alpha_c : -1, premul, col_alpha, fg_a_row, bg_a_row);
			if (at_target)
				fprintf(stderr, "[mj-out] f=%d out=%g,%g,%g a=%g\n", frame_ct,
				    outr[sx_t*4+0], outr[sx_t*4+1], outr[sx_t*4+2], outr[sx_t*4+3]);
		  if (tx >= 0 && f->is_last && y == h - 1) frame_ct++;
		}
		}
	}
	return 0;

MICROJXL__ON_ERROR:
	return st->err;
}/* Coalesced extra-channel canvas maintenance (libjxl keeps the composited
	 * EC planes in dec_state->full_image / frame_storage_for_referencing; the
	 * conformance npy exposes exactly those planes for stills). `frame_ec[i]`
	 * is the frame's own plane for EC i (may be NULL: the EC is then carried
	 * by the canvas only); `blend_mode`/`alpha_chan`/`clamp` come from the
	 * frame's ec_blend_info. Values are raw samples (0..maxval integers, I16
	 * or I32 planes, possibly empty for ECs this frame does not carry), or
	 * normalized floats (the float EC carrier built by upsample_frame for
	 * VarDCT frames' alpha; the blend reads it unchanged).
	 *
	 * Semantics mirror microjxl__frame_blend_row per EC:
	 * - first non-REPLACE frame: seed the canvas from the frame at the crop
	 *   origin, zeroes (or the EC canvas of the referenced background when a
	 *   preceding REPLACE frame stored one) elsewhere;
	 * - REPLACE (only possible for a full-canvas frame): copy the frame;
	 * - other modes: blend by the EC's own alpha channel. */
MICROJXL__STATIC_RETURNS_ERR microjxl__frame_ec_blend_canvas(
	microjxl__st *st, microjxl__plane **frame_ec, int32_t n_frame_ec, int32_t num_ec
) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	int32_t w = im->width, h = im->height;
	int32_t i, y;

#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_SPOT")) {
		fprintf(stderr, "[mj-ecc] begin n_fe=%d num_ec=%d fx0=%d fy0=%d fw=%d fh=%d gmod=%p ch=%p nch=%d\n",
			(int) n_frame_ec, (int) num_ec, f->x0, f->y0, f->width, f->height,
			(void*) &f->gmodular, (void*) f->gmodular.channel, f->gmodular.num_channels);
		for (i = 0; i < n_frame_ec && frame_ec; ++i) {
			microjxl__plane *p = frame_ec[i];
			if (p) fprintf(stderr, "[mj-ecc] raw[%d] p=%p type=%d w=%d h=%d\n", i, (void*) p, (int) p->type, p->width, p->height);
			if (p && p->type != MICROJXL__PLANE_EMPTY && p->width > 0) {
				int32_t mn = 1 << 30, mx = -(1 << 30);
				for (y = 0; y < p->height; ++y) {
					const int16_t *r = p->type == MICROJXL__PLANE_I32 ? NULL : MICROJXL__I16_PIXELS(p, y);
					const int32_t *r3 = p->type == MICROJXL__PLANE_I32 ? MICROJXL__I32_PIXELS(p, y) : NULL;
					int32_t x;
					for (x = 0; x < p->width; ++x) {
						int32_t v = r3 ? r3[x] : r[x];
						if (v < mn) mn = v;
						if (v > mx) mx = v;
					}
				}
				fprintf(stderr, "[mj-ecc] src[%d] type=%d w=%d h=%d min=%d max=%d\n", i, (int) p->type, p->width, p->height, mn, mx);
			} else {
				fprintf(stderr, "[mj-ecc] src[%d] empty\n", i);
			}
		}
	}
#endif
	if (im->num_ec_canvas < num_ec) {
		microjxl__plane *arr = (microjxl__plane *) microjxl__malloc((size_t) num_ec, sizeof(microjxl__plane));
		MICROJXL__SHOULD(arr, "!mem");
		for (i = 0; i < num_ec; ++i) microjxl__init_empty_plane(&arr[i]);
		if (im->ec_canvas) {
			for (i = 0; i < im->num_ec_canvas; ++i) arr[i] = im->ec_canvas[i];
			microjxl__mem_free(im->ec_canvas);
		}
		im->ec_canvas = arr;
		im->num_ec_canvas = num_ec;
	}
	for (i = 0; i < num_ec; ++i) {
		microjxl__plane *dst = &im->ec_canvas[i];
		microjxl__plane *src = (i < n_frame_ec && frame_ec) ? frame_ec[i] : NULL;
		int32_t mode = f->ec_blend_info[i].mode;
		int32_t alpha_chan = f->ec_blend_info[i].alpha_chan;
		int32_t clamp = f->ec_blend_info[i].clamp;			int dst_empty = dst->type == MICROJXL__PLANE_EMPTY;
		int src_empty = !src || src->type == MICROJXL__PLANE_EMPTY;
		int self_alpha = (alpha_chan == i);
		(void) self_alpha;
		/* Blend background for this EC = its OWN blend source slot (libjxl
		 * stage_blending: bg for EC c comes from reference_frames[ec.source];
		 * an absent slot means zeroes). Padding rows/columns outside the
		 * frame rect copy this background too (ProcessPaddingRow), NOT the
		 * accumulated canvas. */
		const microjxl__plane *bgec = NULL;
		{
			int32_t sslot = f->ec_blend_info[i].src_ref_frame;
			if (sslot >= 0 && sslot < 4 && im->ref_ec[sslot] &&
			    i < im->ref_ec_n[sslot] && im->ref_ec[sslot][i].type == MICROJXL__PLANE_F32 &&
			    im->ref_ec[sslot][i].width == w && im->ref_ec[sslot][i].height == h) {
				bgec = &im->ref_ec[sslot][i];
			}
		}
		/* The canvas is float, in each channel's normalized 0..1 domain
		 * (libjxl keeps the full_image extra channels as float pipeline
		 * rows and blends in float — an integer canvas would round kMul/
		 * kBlend results back onto the sample grid, which the conformance
		 * blendmodes case exposes). Missing canvas blends over zeroes. */
		if (src_empty && dst_empty) continue; // nothing to composite for this EC yet
		if (dst->type != MICROJXL__PLANE_F32 || dst->width != w || dst->height != h) {
			microjxl__mem_free_plane(dst);
			MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, w, h, MICROJXL__PLANE_FORCE_PAD, dst));
			dst_empty = 1;
		}
		for (y = 0; y < h; ++y) {
			int32_t fy = y - f->y0;
			int32_t xs = f->x0 < 0 ? 0 : f->x0;
			int32_t xe = f->x0 + f->width > w ? w : f->x0 + f->width;
			float *d = MICROJXL__F32_PIXELS(dst, y);
			const int32_t *s3 = (!src_empty && fy >= 0 && fy < f->height && src->type == MICROJXL__PLANE_I32)
				? MICROJXL__I32_PIXELS(src, fy) : NULL;
			const int16_t *s2 = (!src_empty && fy >= 0 && fy < f->height && src->type == MICROJXL__PLANE_I16)
				? MICROJXL__I16_PIXELS(src, fy) : NULL;
			const float *sf = (!src_empty && fy >= 0 && fy < f->height && src->type == MICROJXL__PLANE_F32)
				? MICROJXL__F32_PIXELS(src, fy) : NULL;
			float fg_scale = (float) microjxl__maxpixel_alpha(&im->ec_info[i]);
			/* canvas alpha row (bg alpha; NULL when absent) */
			const float *bg_a = (alpha_chan >= 0 && alpha_chan < num_ec &&
			                     im->ec_canvas[alpha_chan].type == MICROJXL__PLANE_F32)
				? MICROJXL__F32_PIXELS(&im->ec_canvas[alpha_chan], y) : NULL;
			/* frame alpha row (fg alpha; NULL when the frame does not
			 * carry the alpha EC — self-blend then uses the canvas) */
			const microjxl__plane *ap = (alpha_chan >= 0 && alpha_chan < n_frame_ec && frame_ec &&
			                              frame_ec[alpha_chan] &&
			                              frame_ec[alpha_chan]->type != MICROJXL__PLANE_EMPTY)
				? frame_ec[alpha_chan] : NULL;
			float a_scale = ap ? (float) microjxl__maxpixel_alpha(&im->ec_info[alpha_chan]) : 1.0f;
			int32_t x;
			for (x = 0; x < w; ++x) {
			float bgv = dst_empty ? 0.0f : d[x];
			float bga = (dst_empty || !bg_a) ? 0.0f : bg_a[x];
			float fg, fga;
			int in_rect = x >= xs && x < xe && (s3 || s2 || sf);
			if (!in_rect) {
				/* outside the frame rect: the padding comes from the EC's
				 * blend source slot (libjxl ProcessPaddingRow copies
				 * ec_bg.extra_channels()[ec] there, and MEMSETS TO ZERO
				 * when that slot is absent) — never the accumulated canvas */
				d[x] = bgec ? MICROJXL__F32_PIXELS(bgec, y)[x] : 0.0f;
				continue;
			}
				fg = sf ? sf[x - f->x0]
				        : s3 ? (float) s3[x - f->x0] / fg_scale
				        : (float) s2[x - f->x0] / fg_scale;
				/* fg blend alpha: the frame's alpha EC when carried, else
				 * the canvas alpha (self-blend against a reference) */
				if (ap) {
					/* the frame's alpha EC may be the float carrier (F32, already
					 * normalized) or a raw integer plane — read accordingly */
					fga = ap->type == MICROJXL__PLANE_F32
						? MICROJXL__F32_PIXELS(ap, fy)[x - f->x0]
						: ap->type == MICROJXL__PLANE_I32
							? (float) MICROJXL__I32_PIXELS(ap, fy)[x - f->x0] / a_scale
							: (float) MICROJXL__I16_PIXELS(ap, fy)[x - f->x0] / a_scale;
				} else {
					fga = bga;
				}
				if (clamp) fga = MICROJXL__CLAMP01F(fga);
				switch (mode) {
					case MICROJXL__BLEND_REPLACE:
						d[x] = fg;
						break;
					case MICROJXL__BLEND_ADD:
						/* plain add (libjxl PatchBlendMode::kAdd) */
						d[x] = bgv + fg;
						break;
					case MICROJXL__BLEND_BLEND: {
						/* kBlendAbove */
						int premul = (alpha_chan >= 0 && alpha_chan < num_ec)
							? im->ec_info[alpha_chan].data.alpha_associated : 0;
						if (self_alpha) {
							/* PerformAlphaBlending bg==bga && fg==fga */
							d[x] = 1.0f - (1.0f - fga) * (1.0f - bga);
						} else if (premul) {
							d[x] = fg + bgv * (1.0f - fga);
						} else {
							float na = 1.0f - (1.0f - fga) * (1.0f - bga);
							float rna = na > 0.0f ? 1.0f / na : 0.0f;
							d[x] = (fg * fga + bgv * bga * (1.0f - fga)) * rna;
						}
						break;
					}
					case MICROJXL__BLEND_MUL_ADD:
						if (self_alpha) {
							/* PerformAlphaWeightedAdd fg==fga: bg passthrough */
							d[x] = bgv;
						} else {
							d[x] = bgv + fg * fga;
						}
						break;
					case MICROJXL__BLEND_MUL:
						d[x] = bgv * (clamp ? MICROJXL__CLAMP01F(fg) : fg);
						break;
					default:
						d[x] = bgv;
						break;
				}
			}
		}
	}
	return 0;

MICROJXL__ON_ERROR:
	return st->err;
}

/* Master render: decodes the current frame into an RGBA float plane with
 * samples in [0,1] (display-referred, transfer function already applied).
 * The public pixel accessors (u8x4/u16x4/f32x4) rescale this plane on
 * demand. Missing alpha is opaque (1.0). */
MICROJXL__STATIC_RETURNS_ERR microjxl__render_rgba_f32(microjxl__st *st, microjxl__plane *out) {
	microjxl__image_st *im = st->image;
	microjxl__frame_st *f = st->frame;
	microjxl__plane *c[4], rgba = MICROJXL__INIT;
	int32_t maxpixel, maxpixel2;
	int32_t alpha_maxpixel; // alpha scale, may differ from the colour scale
	int32_t i, x, y;

#ifdef MICROJXL_DEBUG
	if (getenv("MICROJXL_TRACE_ERR")) fprintf(stderr, "[microjxl-render] bpp=%d exp_bits=%d modular=%d xyb=%d\n", im->bpp, im->exp_bits, (int) f->is_modular, (int) im->xyb_encoded);
#endif
	/* Bit depths 1..16 are supported for integer samples; float frames
	 * (exp_bits != 0) can carry bpp up to 32 (their sample values are
	 * floats in [0,1], handled below). cjxl emits bpp<8 for images with
	 * few distinct values (e.g. a flat colour encodes as bpp=1), so this
	 * is a common case, not a curiosity. */
	MICROJXL__SHOULD(im->bpp >= 1 && (im->bpp <= 16 || im->exp_bits != 0), "fbpp");
	/* Float frames: libjxl only converts integer bit patterns to float
	 * for non-XYB modular frames (lossless float); XYB/VarDCT float
	 * frames go through the regular integer pipeline with the [0,1]
	 * output convention. maxpixel=255 gives exactly that. */
	maxpixel = microjxl__maxpixel_scale(im);

	/* VarDCT frames produced half-float render planes (the combine or
	 * finalize_xyb_color): the display-referred colour values live there
	 * (unclamped pipeline floats, matching libjxl); only alpha/ECs come
	 * from gmodular. */
	if (!f->is_modular && f->render_f16[0].type != 0) {
		for (i = 0; i < 3; ++i) c[i] = &f->render_f16[i];
		/* the zeroed gmodular colour planes are placeholders; the ECs
		 * (alpha etc., decoded by lf_global) start after them */
		c[3] = NULL;
		alpha_maxpixel = maxpixel;
		for (i = 3; i < f->gmodular.num_channels; ++i) {
			microjxl__ec_info *ec = &im->ec_info[i - 3];
			if (ec->type == MICROJXL__EC_ALPHA) {
				MICROJXL__SHOULD(ec->exp_bits == 0,
					"TODO: float alpha on VarDCT frames");
				alpha_maxpixel = microjxl__maxpixel_alpha(ec);
				c[3] = &f->gmodular.channel[i];
				break;
			}
		}
		goto have_channels;
	}

	/* Modular channel layout: the colour channels come first (3 for RGB,
	 * 1 for grayscale -- a direct luma channel), then the extra channels. */
	{
		int color_channels;
		MICROJXL__ASSERT(f->gmodular.num_channels >= 1);
		color_channels = (f->gmodular.num_channels >= 3) ? 3 : 1;
		if (f->modular_f_valid) {
			/* finalize_modular_frame filtered the colour rows: consume the
			 * float planes (int/(2^bpp-1) domain, gray already replicated) */
			if (color_channels == 1) {
				for (i = 0; i < 3; ++i) c[i] = &f->modular_f[0];
			} else {
				for (i = 0; i < 3; ++i) c[i] = &f->modular_f[i];
			}
		} else if (color_channels == 1) {
			for (i = 0; i < 3; ++i) c[i] = &f->gmodular.channel[0]; // gray: replicate luma
		} else {
			for (i = 0; i < 3; ++i) c[i] = &f->gmodular.channel[i];
		}
		c[3] = NULL;
		alpha_maxpixel = maxpixel; // alpha scale, defaults to the colour scale
		for (i = color_channels; i < f->gmodular.num_channels; ++i) {
			microjxl__ec_info *ec = &im->ec_info[i - color_channels];
		if (ec->type == MICROJXL__EC_ALPHA) {
			/* Alpha may carry its own bit depth (spec: every EC has bpp and
			 * float_sample_flag); libjxl handles mixed depths by scaling each
			 * channel with its own maxval, so replicate that instead of
			 * rejecting the image. Float sample types must still match the
			 * colour channels (the integer/float pipelines differ). */
			MICROJXL__SHOULD(ec->exp_bits == im->exp_bits,
				"TODO: alpha channel has a different sample type from color channels");
			alpha_maxpixel = microjxl__maxpixel_alpha(ec);
			/* Subsampled alpha (dim_shift > 0) is upsampled to full
			 * resolution by microjxl__upsample_frame before render, so any
			 * stored shift is already resolved here. Associated
			 * (premultiplied) alpha is passed through unchanged: blending
			 * between frames de-premultiplies when needed. */				c[3] = &f->gmodular.channel[i];
				break;
			}
			}
		}

	/* Spot colour extra channels: libjxl's SpotColorStage reads the EC
	 * rows directly and blends them onto the pipeline colour channels;
	 * the stage order in dec_cache.cc is XYB -> spot -> tone map -> TF,
	 * so for XYB modular frames the mix must happen on LINEAR values
	 * (inside the pixel loop below). For every other frame the pixel
	 * loop already yields display-referred samples and the mix runs as
	 * a post-pass after it. This block only scans the EC layout. */
have_channels:;
	int color_channels2 = (f->gmodular.num_channels >= 3) ? 3 : 1;
	microjxl__plane *spot_plane[8];
	float spot_col[8][4];
	float spot_scale[8];
	int nspot = 0, k;
	if (getenv("MICROJXL_TRACE_SPOT")) fprintf(stderr, "[microjxl-spot] scan: nch=%d w=%d h=%d nec=%d cc2=%d\n", f->gmodular.num_channels, f->width, f->height, im->num_extra_channels, color_channels2);
		for (k = color_channels2; k < f->gmodular.num_channels && nspot < 8; ++k) {
			int eck = k - color_channels2;
			microjxl__ec_info *ec;
			if (eck >= im->num_extra_channels) break;
			ec = &im->ec_info[eck];
			if (getenv("MICROJXL_TRACE_SPOT")) fprintf(stderr, "[microjxl-spot] k=%d eck=%d type=%d chw=%d chh=%d\n", k, eck, (int) ec->type, f->gmodular.channel[k].width, f->gmodular.channel[k].height);
			if (ec->type != MICROJXL__EC_SPOT_COLOUR) continue;
			if (f->gmodular.channel[k].width != f->width ||
			    f->gmodular.channel[k].height != f->height) continue;
			spot_plane[nspot] = &f->gmodular.channel[k];
			spot_col[nspot][0] = ec->data.spot.red;
			spot_col[nspot][1] = ec->data.spot.green;
			spot_col[nspot][2] = ec->data.spot.blue;
			spot_col[nspot][3] = ec->data.spot.solidity;
			spot_scale[nspot] = ec->exp_bits != 0 ? 0.0f
				: (float) (((int32_t) 1 << ec->bpp) - 1);
			++nspot;
		}
	(void) spot_plane; (void) spot_col; (void) spot_scale;

#ifdef MICROJXL_DEBUG
	fprintf(stderr, "[microjxl] render: f=%dx%d up=%dx%d gmodular %d ch bpp=%d, c3=%s (%p), dims %dx%d %dx%d %dx%d, m_lf_scaled=%g/%g/%g, types=%d/%d/%d/%d\n",
		f->width, f->height, f->upsampled_width, f->upsampled_height,
		f->gmodular.num_channels, im->bpp, c[3] ? "set" : "null", (void*) c[3],
		c[0] ? c[0]->width : -1, c[0] ? c[0]->height : -1, c[1] ? c[1]->width : -1, c[1] ? c[1]->height : -1,
		c[2] ? c[2]->width : -1, c[2] ? c[2]->height : -1, f->m_lf_scaled[0], f->m_lf_scaled[1], f->m_lf_scaled[2],
		c[0] ? c[0]->type : -1, c[1] ? c[1]->type : -1, c[2] ? c[2]->type : -1, c[3] ? c[3]->type : -1);
#endif
	MICROJXL__SHOULD(f->width < INT32_MAX / 4, "bigg");
	/* the render is always FRAME-sized; canvas compositing (crops, blend
	 * modes) happens afterwards in next_frame (microjxl__frame_blend_canvas),
	 * mirroring libjxl where the BlendingStage switches the pipeline to the
	 * image dimensions and composites over the referenced background */
	MICROJXL__TRY(microjxl__init_plane(st, MICROJXL__PLANE_F32, f->width * 4, f->height, MICROJXL__PLANE_FORCE_PAD, &rgba));

	(void) maxpixel2; /* retained for API stability; scaling uses maxpixel */
	/* Restoration filters for modular frames run in
	 * microjxl__finalize_modular_frame (pre-upsampling, libjxl stage
	 * order: gaborish -> EPF -> patches -> splines -> upsample), on the
	 * float pipeline rows with the constant sigma (epf.sigma_for_modular)
	 * for EPF; the render only reads the already-filtered channels. */
	/* Modular channels are stored as signed 16-bit planes when
	 * modular_16bit_buffers is set, signed 32-bit otherwise (the
	 * inverse transforms can exceed the 16-bit range for lossless and
	 * high-bit-depth inputs). Read either kind. */
	for (y = 0; y < f->height; ++y) {
		int16_t *pixels16[4];
		int32_t *pixels32[4];
		uint16_t *pixelsf16[4];
		float *pixelsf32[4];
		int is_f16 = c[0]->type == MICROJXL__PLANE_F32; // VarDCT float pipeline
		{
		float *outpixels = MICROJXL__F32_PIXELS(&rgba, y);
		for (i = 0; i < 4; ++i) {
			pixels16[i] = NULL;
			pixels32[i] = NULL;
			pixelsf16[i] = NULL;
			pixelsf32[i] = NULL;
			if (c[i]) {
				if (c[i]->type == MICROJXL__PLANE_I32)
					pixels32[i] = MICROJXL__I32_PIXELS(c[i], y);
				else if (c[i]->type == MICROJXL__PLANE_F32)
					pixelsf32[i] = MICROJXL__F32_PIXELS(c[i], y);
				else if (c[i]->type == MICROJXL__PLANE_F16)
					pixelsf16[i] = MICROJXL__F16_PIXELS(c[i], y);
				else
					pixels16[i] = MICROJXL__I16_PIXELS(c[i], y);
			}
		}
#ifdef MICROJXL_DEBUG
		if (y == 0 && x < 8 && !f->is_modular) {
			fprintf(stderr, "[microjxl-r] px%d: p0=%d p1=%d p2=%d (is_mod=%d xyb=%d bpp=%d)\n",
				x, pixels16[0] ? pixels16[0][x] : -999, pixels16[1] ? pixels16[1][x] : -999,
				pixels16[2] ? pixels16[2][x] : -999, (int) f->is_modular, (int) im->xyb_encoded, im->bpp);
		}
#endif
		for (x = 0; x < f->width; ++x) {
		int32_t p[4];
		float pf[4];
		for (i = 0; i < 4; ++i) {
			if (!c[i]) {
				p[i] = maxpixel;
				pf[i] = 1.0f;
			} else if (pixels32[i]) {
				p[i] = pixels32[i][x];
				pf[i] = 0.0f;
			} else if (pixelsf32[i]) {
				p[i] = 0; /* float colour channel: read separately below */
				pf[i] = pixelsf32[i][x];
			} else {
				p[i] = pixels16[i][x];
				pf[i] = 0.0f;
			}
		}
			if (is_f16) {
				/* VarDCT float pipeline: the colour channels are already
				 * display-referred (TF applied, unclamped float32); alpha/EC
				 * keep the integer path below. */
				for (i = 0; i < 3; ++i)
					outpixels[x * 4 + i] = pixelsf32[i][x];
				/* alpha: the float EC carrier (ec_to_float_plane, normalized
				 * 0..1) when present, else the integer grid */
				outpixels[x * 4 + 3] = pixelsf32[3] ? pf[3]
					: c[3] ? (float) p[3] / (float) alpha_maxpixel
					: 1.0f;
				continue;
			}
#ifdef MICROJXL_DEBUG
			if (f->is_modular && im->xyb_encoded && y == 0 && x == 0) {
				float X = (float) p[1] * f->m_lf_scaled[0];
				float Y = (float) p[0] * f->m_lf_scaled[1];
				float B = (float) (p[2] + p[0]) * f->m_lf_scaled[2];
				fprintf(stderr, "[microjxl] xyb p0=%d p1=%d p2=%d (mf=%g/%g/%g) -> X=%g Y=%g B=%g\n", p[0], p[1], p[2],
					f->m_lf_scaled[0], f->m_lf_scaled[1], f->m_lf_scaled[2], X, Y, B);
			}
#endif
				if (f->is_modular && im->xyb_encoded) {
					/* Lossy modular frames store the inverse-opsin (XYB) samples:
					 * channel 0 = Y*InvDC1, channel 1 = X*InvDC0, channel 2 =
					 * B*InvDC2 - Y*InvDC1 (see libjxl dec_modular / enc_modular).
					 * Undo the scaling and reconstruct B, then apply XYB -> sRGB
					 * (the opsin bias is negative, see the OPSIN_BIAS comment). */
					float X = (float) p[1] * f->m_lf_scaled[0];
					float Y = (float) p[0] * f->m_lf_scaled[1];
					float B = (float) (p[2] + p[0]) * f->m_lf_scaled[2];
					/* patches and splines were already applied at stored
					 * resolution (apply_patches_modular in the frame loop,
					 * microjxl__finalize_modular_frame) — libjxl stage order has
					 * them pre-upsampling; do NOT apply them again here. */
					if (f->has_noise) {
						MICROJXL__TRY(microjxl__add_noise_frame(st));
						microjxl__apply_noise_xyb(f, f->noise_planes, x, y, &X, &Y, &B);
					}
				float gammas[3] = {
					Y + X - cbrtf(im->opsin_bias[0]),
					Y - X - cbrtf(im->opsin_bias[1]),
					B - cbrtf(im->opsin_bias[2]),
				};
				/* libjxl folds 255/intensity_target into the inverse opsin matrix
				 * (InitSIMDInverseMatrix); microjxl keeps the matrix unscaled and applies
				 * the scale here, mirroring the VarDCT path (itscale). */
				float itscale = 255.0f / im->intensity_target;
				float mixed[3];
				float ootf;
				for (i = 0; i < 3; ++i) {
					mixed[i] = (gammas[i] * gammas[i] * gammas[i] + im->opsin_bias[i]) * itscale;
				}
				ootf = microjxl__hlg_ootf_ratio(im, mixed[0], mixed[1], mixed[2]);
				for (i = 0; i < 3; ++i) {
					float v =
						im->opsin_inv_mat[i][0] * mixed[0] +
						im->opsin_inv_mat[i][1] * mixed[1] +
						im->opsin_inv_mat[i][2] * mixed[2];
					v *= ootf;
					/* libjxl's SpotColorStage runs on linear pipeline rows
					 * (stage order: XYB -> spot -> TF); blend the spot ECs
					 * here, before the transfer function. */
					for (k = 0; im->render_spot && k < nspot; ++k) {
						int32_t sraw = spot_plane[k]->type == MICROJXL__PLANE_I32
							? MICROJXL__I32_PIXELS(spot_plane[k], y)[x]
							: MICROJXL__I16_PIXELS(spot_plane[k], y)[x];
						float s = spot_scale[k] > 0.0f
							? (float) sraw / spot_scale[k]
							: microjxl__int_to_float(sraw, im->ec_info[k - color_channels2].bpp, im->ec_info[k - color_channels2].exp_bits);
						float mix = spot_col[k][3] * s;
						v = mix * spot_col[k][i] + (1.0f - mix) * v;
					}
					// to the image's transfer function (linear XYB -> tf);
					// want_icc images keep the rows linear (TF lives in the ICC)
					if (!im->want_icc) v = microjxl__tf_encode(im, v);
					/* libjxl's render pipeline carries display-referred floats
					 * unclamped and only quantizes at the u8/u16 output stage;
					 * the f32 plane must keep that precision (the u8/u16
					 * conversions below reproduce the old round-half-up+clamp
					 * bit-for-bit from the unquantized float). */
					outpixels[x * 4 + i] = v;
				}
				outpixels[x * 4 + 3] = (float) p[3] / (float) alpha_maxpixel;
			} else if (im->exp_bits != 0 && f->is_modular) {
				/* Lossless (kNone) float frames: sample values are custom-width
				 * float bit patterns decoded through the modular integer
				 * pipeline. Convert each channel back to float and map to 8-bit:
				 * values are display-referred in [0,1] (clamp), and apply the
				 * sRGB transfer when the metadata says the samples are linear. */
				for (i = 0; i < 4; ++i) {
					float v;
					if (!c[i]) {
						v = 1.0f; /* missing alpha: opaque */
					} else {
						v = microjxl__int_to_float(p[i], im->bpp, im->exp_bits);
						/* splines add to the float channel values (libjxl draw
						 * stage operates on pipeline rows pre-colour-transform) */
						if (i < 3 && f->has_splines && f->num_segments) {
							float d[3];
							microjxl__spline_deltas_at(f, x >> f->log_upsampling, y >> f->log_upsampling, d);
							v += d[i];
						}
						if (i < 3 && im->gamma_or_tf == MICROJXL__TF_LINEAR)
							v = (v <= 0.0031308f ? 12.92f * v : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f);
					}
					/* keep the pipeline float unclamped (libjxl stage order); the
					 * u8/u16 conversions clamp quantized results downstream */
					outpixels[x * 4 + i] = v;
				}
			} else {
				/* splines on plain integer channels: libjxl's pipeline rows carry
				 * int * 1/((1<<bpp)-1) floats; splines were already applied at
				 * stored resolution by the combine (VarDCT) or
				 * microjxl__finalize_modular_frame (modular) — adding the deltas
				 * here a second time would double-draw them */
				for (i = 0; i < 4; ++i) {
					int32_t chmax = (i == 3) ? alpha_maxpixel : maxpixel;
					/* libjxl's pipeline rows keep the raw (possibly overshooting)
					 * sample; only the u8/u16 output conversions clamp */
					outpixels[x * 4 + i] = (float) p[i] / (float) chmax;
				}
			}
		}
		}
	}

	/* Spot colour for non-XYB frames: the pixel loop already produced
	 * display-referred values, so the mix runs here (for kNone/kYCbCr
	 * frames the pipeline rows are display-referred all along). XYB
	 * frames were handled inside the pixel loop, on linear values
	 * (libjxl stage order: XYB -> spot -> TF). */
	if (nspot > 0 && im->render_spot && !(f->is_modular && im->xyb_encoded)) {
		static const int32_t cc2 = 0; (void) cc2;
		int16_t *sp16[8];
		int32_t *sp32[8];
		if (getenv("MICROJXL_TRACE_SPOT")) fprintf(stderr, "[microjxl-spot] nspot=%d blend begin w=%d h=%d ftype=%d modular=%d\n", nspot, f->width, f->height, (int) f->type, (int) f->is_modular);
		for (y = 0; y < f->height; ++y) {
			float *outpixels = MICROJXL__F32_PIXELS(&rgba, y);
			for (k = 0; k < nspot; ++k) {
				sp16[k] = spot_plane[k]->type == MICROJXL__PLANE_I32 ? NULL
					: MICROJXL__I16_PIXELS(spot_plane[k], y);
				sp32[k] = spot_plane[k]->type == MICROJXL__PLANE_I32 ? MICROJXL__I32_PIXELS(spot_plane[k], y)
					: NULL;
			}
			for (x = 0; x < f->width; ++x) {
				if (getenv("MICROJXL_SPOTDUMP") && y == 124 && x >= 160 && x < 165)
					fprintf(stderr, "[mspot] y=%d x=%d k0 sraw=%d\n", y, x, sp16[0] ? sp16[0][x] : (sp32[0] ? sp32[0][x] : -1));
				for (k = 0; k < nspot; ++k) {
					int32_t sraw = sp16[k] ? sp16[k][x] : sp32[k][x];
					float s, mix;
					int c2;
					if (spot_scale[k] > 0.0f) s = (float) sraw / spot_scale[k];
					else s = microjxl__int_to_float(sraw, im->ec_info[k - color_channels2].bpp, im->ec_info[k - color_channels2].exp_bits);
					mix = spot_col[k][3] * s;
					if (getenv("MICROJXL_SPOTDUMP") && y == 124 && x >= 160 && x < 165)
						fprintf(stderr, "[mspot] k=%d s=%g mix=%g base=(%g,%g,%g)\n", k, s, mix, outpixels[x*4+0], outpixels[x*4+1], outpixels[x*4+2]);
					for (c2 = 0; c2 < 3; ++c2) {
						float p = outpixels[x * 4 + c2];
						outpixels[x * 4 + c2] = mix * spot_col[k][c2] + (1.0f - mix) * p;
					}
				}
			}
		}
	}

	*out = rgba;
	return 0;

MICROJXL__ON_ERROR:
	microjxl__mem_free_plane(&rgba);
	return st->err;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// API utilities

// we don't trust callers and do the basic check ourselves
#define MICROJXL__IMAGE_MAGIC ((uint32_t) 0x7867ae21) // crc32("microjxl_image")
#define MICROJXL__IMAGE_ERR_MAGIC ((uint32_t) 0xb26a48aa) // crc32("microjxl_image with error")
#define MICROJXL__IMAGE_OPEN_ERR_MAGIC ((uint32_t) 0x02c2eb6d) // crc32("microjxl_image with open error")
#define MICROJXL__FRAME_MAGIC ((uint32_t) 0x08a296b3) // crc32("microjxl_frame")
#define MICROJXL__FRAME_ERR_MAGIC ((uint32_t) 0x16351564) // crc32("microjxl_frame with error")
#define MICROJXL__INNER_MAGIC ((uint32_t) 0x5009e1c4) // crc32("microjxl__inner")

#define MICROJXL__FOREACH_API(X) \
	X(from_file,) \
	X(from_memory,) \
	/* the last origin that can use alternative magic numbers, see MICROJXL__ORIGIN_LAST_ALT_MAGIC */ \
	X(output_format,) \
	X(set_coalescing,) \
	X(set_render_spot,) \
	X(next_frame,) \
	X(current_frame,) \
	X(frame_pixels,_*) \
	X(error_string,) \
	X(free,) \

typedef enum { // each API defines its origin value; they don't have to be stable
	MICROJXL__ORIGIN_NONE = 0,
	MICROJXL__ORIGIN_NEXT, // for microjxl_free; the next call will be the actual origin
#define MICROJXL__ORIGIN_ENUM_VALUE(origin, suffix) MICROJXL__ORIGIN_##origin,
	MICROJXL__FOREACH_API(MICROJXL__ORIGIN_ENUM_VALUE)
	MICROJXL__ORIGIN_MAX,
	MICROJXL__ORIGIN_LAST_ALT_MAGIC = MICROJXL__ORIGIN_from_memory,
} microjxl__origin;

static const char *MICROJXL__ORIGIN_NAMES[] = {
	"(unknown)",
	NULL,
#define MICROJXL__ORIGIN_NAME(origin, suffix) #origin #suffix,
	MICROJXL__FOREACH_API(MICROJXL__ORIGIN_NAME)
};

static const struct { char err[5]; const char *msg, *suffix; } MICROJXL__ERROR_STRINGS[] = {
	{ "Upt0", "`path` parameter is NULL", NULL },
	{ "Ubf0", "`buf` parameter is NULL", NULL },
	{ "Uch?", "Bad `channel` parameter", NULL },
	{ "Ufm?", "Bad `format` parameter", NULL },
	{ "Uof?", "Bad `channel` and `format` combination", NULL },
	{ "Urnd", "Frame is not yet rendered", NULL },
	{ "Ufre", "Trying to reuse already freed image", NULL },
	{ "!mem", "Out of memory", NULL },
	{ "!jxl", "The JPEG XL signature is not found", NULL },
	{ "open", "Failed to open file", NULL },
	{ "bigg", "Image dimensions are too large to handle", NULL },
	{ "flen", "File is too lengthy to handle", NULL },
	{ "shrt", "Premature end of file", NULL },
	{ "slim", "Image size limit reached", NULL },
	{ "elim", "Extra channel number limit reached", NULL },
	{ "xlim", "Modular transform limit reached", NULL },
	{ "tlim", "Meta-adaptive tree size or depth limit reached", NULL },
	{ "plim", "ICC profile length limit reached", NULL },
	{ "fbpp", "Given bits per pixel value is disallowed", NULL }, // "f" stands for "forbidden"
	{ "flvl", "LF frame level is out of range", NULL },
	{ "fblk", "Black extra channel is disallowed", NULL },
	{ "fm32", "32-bit buffers for modular encoding are disallowed", NULL },
	{ "spln", "Spline data is invalid", NULL },
	{ "TODO", "Unimplemented feature encountered", NULL }, // TODO remove this when ready
	{ "TEST", "Testing-only error occurred", NULL },
};

// an API-level twin of `microjxl__st`; see `microjxl__st` documentation for the rationale for split.
typedef struct microjxl__inner {
	uint32_t magic; // should be MICROJXL__INNER_MAGIC

	//microjxl__mutex mutex;

	microjxl__origin origin; // error origin
	// same to those in microjxl__st
	microjxl_err err;
	int saved_errno;
	int cannot_retry;

	#define MICROJXL__ERRBUF_LEN 256
	char errbuf[MICROJXL__ERRBUF_LEN];

	int state; // used in microjxl_advance

	// subsystem contexts; copied to and from microjxl__st whenever needed
	struct microjxl__bits_st bits;
	struct microjxl__source_st source;
	struct microjxl__container_st container;
	struct microjxl__buffer_st buffer;
	struct microjxl__image_st image;
	struct microjxl__frame_st frame;
	struct microjxl__lf_group_st *lf_groups; // [frame.num_lf_groups]

	microjxl__toc toc;

	int rendered;
	microjxl__plane rendered_rgba; // master render: RGBA floats in [0,1]
	microjxl__plane rendered_u8; // cached u8x4 conversion of rendered_rgba
	microjxl__plane rendered_u16; // cached u16x4 conversion of rendered_rgba
	/* cached planar (single-channel) conversions of rendered_rgba;
	 * row 0 = U8, row 1 = U16, row 2 = F32; column 0..3 = R,G,B,A.
	 * Allocated lazily by the planar accessors, invalidated with the
	 * other render caches by next_frame/free. */
	microjxl__plane rendered_planar[3][4];
	/* cached float conversion of extra channel `rendered_ec_idx` (or empty
	 * when none was requested yet); invalidated with the render caches */
	microjxl__plane rendered_ec;
	int32_t rendered_ec_idx;
	/* the frame's own EC planes, kept alive between the EC-canvas pass and
	 * the canvas colour blend (whose fg alpha reads the alpha-EC plane;
	 * render_rgba_f32 may consume the gmodular channels). fb_ec points at
	 * fb_ec_ptrs when n fits, otherwise at a heap array (owned, freed with
	 * the render caches). */
	microjxl__plane **fb_ec;
	int32_t fb_ec_n;
	/* Pre-frame snapshot of the EC-canvas alpha plane (libjxl
	 * PerformBlending: "Blend extra channels first so that we use the
	 * pre-blending alpha" — the colour blend must read the canvas alpha
	 * BEFORE this frame's EC blend updates it). Owned; taken in next_frame
	 * before frame_ec_blend_canvas, unreferenced + freed by
	 * frame_blend_canvas / the no-blend frame path. */
	microjxl__plane *ec_alpha_pre;
	microjxl__plane *fb_ec_ptrs[8];

	int image_done; // set once the last frame of the image has been decoded
	int frame_state_live; // inner->frame holds a decoded (or in-progress) frame
	/* coalescing (canvas compositing) mode; default 1 (libjxl-compatible).
	 * Must be set before the first next_frame call. */
	int coalescing;
	/* spot-colour rendering toggle; stored here because image_metadata()
	 * runs lazily on the first next_frame and resets the user-facing field. */
	int render_spot_pending;
	/* geometry/metadata of the frame currently exposed to the API,
	 * captured when advance() decodes the frame header */
	int32_t frame_x0, frame_y0, frame_width, frame_height;
	int64_t frame_duration;
	int frame_is_last;
	/* lazily generated ICC profile for enum colour encodings
	 * (microjxl_icc_profile caches its result here; owned) */
	void *gen_icc;
	size_t gen_icc_size;
	/* lazily generated ICC of the *output* colour encoding
	 * (microjxl_output_icc_profile; owned) */
	void *gen_out_icc;
	size_t gen_out_icc_size;
	/* JPEG reconstruction output (Part 2 §9.10): assembled by
	 * microjxl__jpeg_reconstruct_from_frame after the last frame decoded;
	 * NULL when the container has no jbrd box or reconstruction failed.
	 * Handed out (not owned by caller) by microjxl_jpeg_reconstruction;
	 * freed here. */
	uint8_t *jpeg_out;
	size_t jpeg_out_size;
} microjxl__inner;

MICROJXL__STATIC_RETURNS_ERR microjxl__set_alt_magic(
	microjxl_err err, int saved_errno, microjxl__origin origin, microjxl_image *image
);
MICROJXL__STATIC_RETURNS_ERR microjxl__set_magic(microjxl__inner *inner, microjxl_image *image);

MICROJXL_STATIC microjxl_err microjxl__check_image(microjxl_image *image, microjxl__origin neworigin, microjxl__inner **outinner);
#define MICROJXL__CHECK_IMAGE() do { \
		microjxl_err err = microjxl__check_image((microjxl_image*) image, ORIGIN, &inner); \
		if (err) return err; \
	} while (0)
#define MICROJXL__SET_INNER_ERR(s) (inner->origin = ORIGIN, inner->err = MICROJXL__4(s))

MICROJXL_STATIC void microjxl__init_state(microjxl__st *st, microjxl__inner *inner);
MICROJXL_STATIC void microjxl__save_state(microjxl__st *st, microjxl__inner *inner, microjxl__origin origin);

MICROJXL__STATIC_RETURNS_ERR microjxl__advance(microjxl__inner *inner, microjxl__origin origin/*, int32_t until*/);

MICROJXL_STATIC void microjxl__mem_free_inner(microjxl__inner *inner);

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL__STATIC_RETURNS_ERR microjxl__set_alt_magic(
	microjxl_err err, int saved_errno, microjxl__origin origin, microjxl_image *image
) {
	if (err == MICROJXL__4("open")) {
		image->magic = MICROJXL__IMAGE_OPEN_ERR_MAGIC ^ (uint32_t) origin;
		image->u.saved_errno = saved_errno;
		return err;
	} else {
		image->magic = MICROJXL__IMAGE_ERR_MAGIC ^ (uint32_t) origin;
		return image->u.err = err;
	}
}

MICROJXL__STATIC_RETURNS_ERR microjxl__set_magic(microjxl__inner *inner, microjxl_image *image) {
	image->magic = MICROJXL__IMAGE_MAGIC;
	image->u.inner = inner;
	inner->magic = MICROJXL__INNER_MAGIC;
	/* coalescing (canvas compositing) defaults to enabled, libjxl-compatible;
	 * set once at open so a later user microjxl_set_coalescing call sticks. */
	inner->coalescing = 1;
	inner->render_spot_pending = 1;
	return 0;
}

MICROJXL_STATIC microjxl_err microjxl__check_image(microjxl_image *image, microjxl__origin neworigin, microjxl__inner **outinner) {
	*outinner = NULL;
	if (!image) return MICROJXL__4("Uim0");
	if (image->magic != MICROJXL__IMAGE_MAGIC) {
		uint32_t origin = image->magic ^ MICROJXL__IMAGE_ERR_MAGIC;
		if (0 < origin && origin <= MICROJXL__ORIGIN_LAST_ALT_MAGIC) {
			if (origin == MICROJXL__ORIGIN_NEXT && neworigin) image->magic = MICROJXL__IMAGE_ERR_MAGIC ^ neworigin;
			return image->u.err;
		}
		origin = image->magic ^ MICROJXL__IMAGE_OPEN_ERR_MAGIC;
		if (0 < origin && origin <= MICROJXL__ORIGIN_LAST_ALT_MAGIC) return MICROJXL__4("open");
		return MICROJXL__4("Uim?");
	}
	if (!image->u.inner || image->u.inner->magic != MICROJXL__INNER_MAGIC) return MICROJXL__4("Uim?");
	*outinner = image->u.inner;
	return image->u.inner->err; // TODO handle cannot_retry in a better way
}

MICROJXL_STATIC void microjxl__init_state(microjxl__st *st, microjxl__inner *inner) {
	st->err = 0;
	st->saved_errno = 0;
	st->cannot_retry = 0;
	st->bits = inner->buffer.checkpoint;
	st->source = &inner->source;
	st->container = &inner->container;
	st->buffer = &inner->buffer;
	st->image = &inner->image;
	st->frame = &inner->frame;
	/* 18181-2 C.3: a jxll box selects the capability limits. With no jxll
	 * box the spec default is level 5, but real-world encoders (e.g.
	 * cjxl --responsive) emit squeeze transform counts above the level 5
	 * cap while libjxl's decoder does not enforce level limits; default to
	 * the level 10 (superset) limits unless level 5 is explicitly signalled. */
	st->limits = (st->container->level == 5) ?
		&MICROJXL__MAIN_LV5_LIMITS : &MICROJXL__MAIN_LV10_LIMITS;
}

MICROJXL_STATIC void microjxl__save_state(microjxl__st *st, microjxl__inner *inner, microjxl__origin origin) {
	if (st->err) {
		inner->origin = origin;
		inner->err = st->err;
		inner->saved_errno = st->saved_errno;
		inner->cannot_retry = st->cannot_retry;
	} else {
		inner->buffer.checkpoint = st->bits;
	}
}

// TODO expose this with a proper interface
MICROJXL__STATIC_RETURNS_ERR microjxl__advance(microjxl__inner *inner, microjxl__origin origin/*, int32_t until*/) {
	microjxl__st stbuf, *st = &stbuf;
	microjxl__frame_st *f;
	microjxl_err err;
#ifdef MICROJXL_DEBUG
	int32_t di;
#endif

	microjxl__init_state(st, inner);

	// a less-known coroutine hack with some tweak.
	// see https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html for basic concepts.
	//
	// it is EXTREMELY important that any `MICROJXL__YIELD_AFTER` call may fail, and the next call
	// to `microjxl_advance` will restart after the last successful `MICROJXL__YIELD_AFTER` call.
	// therefore any code between two `MICROJXL__YIELD_AFTER` can run multiple times!
	// if you don't want this, you should move the code into a separate function.
	// for the same reason, this block can't contain any variable declaration or assignment.
	#define MICROJXL__YIELD_AFTER(expr) \
		do { \
			err = (expr); \
			microjxl__save_state(st, inner, origin); \
			if (err) return err; \
			inner->state = __LINE__; /* thus each line can have at most one MICROJXL__YIELD() call */ \
			/* fall through */ \
			case __LINE__:; \
		} while (0)

	/* States >= 1 are parking spots for the resumable frame loop. */
	#define MICROJXL__STATE_PARKED_FRAME 1

	/* Parks the coroutine after a rendered frame: saves the state and
	 * returns so the API layer can hand the pixels out; the next
	 * `microjxl_next_frame` call re-enters at `case MICROJXL__STATE_PARKED_FRAME`. */
	#define MICROJXL__PARK(park_state_) \
		do { \
			inner->state = (park_state_); \
			microjxl__save_state(st, inner, origin); \
			return 0; \
		} while (0)

	f = st->frame;
	switch (inner->state) {
	case 0: // initial state

		MICROJXL__YIELD_AFTER(microjxl__init_buffer(st, 0, INT64_MAX));
		MICROJXL__YIELD_AFTER(microjxl__signature(st));
		MICROJXL__YIELD_AFTER(microjxl__image_metadata(st));
		/* image_metadata() resets the spot toggle to the default; re-apply a
		 * value the user set via microjxl_set_render_spot before decoding. */
		st->image->render_spot = inner->render_spot_pending;

		if (st->image->want_icc) {
			MICROJXL__YIELD_AFTER(microjxl__icc(st));
		}

		/* A preview is a self-contained regular frame (level=0,
		 * save_as_reference=0) that precedes the main frame. It is only
		 * useful for progressive display, so we parse its header and TOC
		 * and skip straight past its data; the main frame is independent
		 * of it. (libjxl does the same when the preview is not requested.) */
		if (st->image->preview_w > 0 && st->image->preview_h > 0) {
			MICROJXL__YIELD_AFTER(microjxl__frame_header(st));
#ifdef MICROJXL_DEBUG
			fprintf(stderr, "[microjxl] preview skip: phw=%d phh=%d fw=%d fh=%d last=%d dur=%d x0=%d y0=%d\n",
				(int) st->image->preview_w, (int) st->image->preview_h, f->width, f->height,
				(int) f->is_last, (int) f->duration, (int) f->x0, (int) f->y0);
#endif
			MICROJXL__YIELD_AFTER(microjxl__read_toc(st, &inner->toc));
			MICROJXL__YIELD_AFTER(microjxl__seek_buffer(st, inner->toc.end_codeoff));
			microjxl__mem_free_toc(&inner->toc);
			memset(&inner->toc, 0, sizeof inner->toc);
			microjxl__mem_free_frame_state(st->frame);
		}	{ // frame loop: libjxl decodes frames until the last one; REFONLY
	  // frames (frame_type 2) are decoded, saved into a reference slot,
	  // and the loop continues with the next frame.
				for (;;) { // frame loop (re-entered via `continue_frame_loop`)
				continue_frame_loop:
				MICROJXL__YIELD_AFTER(microjxl__frame_header(st));
				inner->frame_state_live = 1;
			/* K.5.2 frame counters (libjxl dec_frame.cc InitFrame, before the
			 * TOC is read): visible iff (REGULAR||SKIPPROG) && (is_last||duration>0);
			 * a visible frame resets the non-visible counter. */
			if ((f->type == MICROJXL__FRAME_REGULAR || f->type == MICROJXL__FRAME_REGULAR_SKIPPROG) &&
				(f->is_last || f->duration > 0)) {
				++st->image->vis_frame_idx;
				st->image->nonvis_frame_idx = 0;
			} else {
				++st->image->nonvis_frame_idx;
			}
#ifdef MICROJXL_DEBUG
			fprintf(stderr, "[microjxl] frame type=%d last=%d modular=%d patches=%d lf_level=%d use_lf=%d vis=%lld nonvis=%lld\n",
				f->type, f->is_last, f->is_modular, f->has_patches, f->lf_level, f->use_lf_frame,
				(long long) st->image->vis_frame_idx, (long long) st->image->nonvis_frame_idx);
			fprintf(stderr, "[microjxl]   geom=%dx%d+%d+%d save_ref=%d blend: mode=%d alpha=%d clamp=%d src_ref=%d restore=%d\n",
				f->width, f->height, (int) f->x0, (int) f->y0, (int) f->save_as_ref,
				(int) f->blend_info.mode, (int) f->blend_info.alpha_chan, (int) f->blend_info.clamp,
				(int) f->blend_info.src_ref_frame, (int) f->is_last);
			{
				int eci;
				/* ec_blend_info is only allocated for REGULAR/SKIPPROG frames */
				for (eci = 0; f->ec_blend_info && eci < st->image->num_extra_channels && eci < 16; ++eci)
					fprintf(stderr, "[microjxl]   ec[%d] mode=%d alpha=%d clamp=%d\n", eci,
						(int) f->ec_blend_info[eci].mode, (int) f->ec_blend_info[eci].alpha_chan,
						(int) f->ec_blend_info[eci].clamp);
			}
#endif
			MICROJXL__YIELD_AFTER(microjxl__read_toc(st, &inner->toc));

			MICROJXL__YIELD_AFTER(microjxl__lf_global_in_section(st, &inner->toc));
			MICROJXL__YIELD_AFTER(microjxl__allocate_lf_groups(st, &inner->lf_groups));

			if (inner->toc.single_size) {
				/* A single-size frame stores every section in one continuous
				 * stream, in the order: global (LF global + DC info), LF
				 * group, AC global (dequant matrices + orders + coefficient
				 * histograms), AC group. The LF group data therefore sits
				 * between the LF global and the AC global data, so it must be
				 * decoded before hf_global here (libjxl: ProcessDCGroup runs
				 * before ProcessACGlobal). Multi-section frames are unaffected
				 * because every section has its own offset. */
				MICROJXL__ASSERT(f->num_lf_groups == 1 && f->num_groups == 1 && f->num_passes == 1);
				MICROJXL__YIELD_AFTER(microjxl__lf_group(st, &inner->lf_groups[0]));
				MICROJXL__YIELD_AFTER(microjxl__hf_global_in_section(st, &inner->toc));
				MICROJXL__YIELD_AFTER(microjxl__prepare_dq_matrices(st));
				MICROJXL__YIELD_AFTER(microjxl__prepare_orders(st));
				MICROJXL__YIELD_AFTER(microjxl__pass_group(st, 0, 0, 0, f->width, f->height, 0, &inner->lf_groups[0]));
				MICROJXL__YIELD_AFTER(microjxl__zero_pad_to_byte(st));
			} else {
				MICROJXL__YIELD_AFTER(microjxl__hf_global_in_section(st, &inner->toc));
				while (inner->toc.nsections_read < inner->toc.nsections) {
					MICROJXL__YIELD_AFTER(microjxl__lf_or_pass_group_in_section(st, &inner->toc, inner->lf_groups));
				}
			}

			MICROJXL__YIELD_AFTER(microjxl__end_of_frame(st, &inner->toc));
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_DUMP_FPRE")) {
				static int fprectr = 0;
				char pfn[64];
				snprintf(pfn, sizeof(pfn), "/tmp/mj_fpre_%d.bin", fprectr++);
				FILE *pf = fopen(pfn, "wb");
				int32_t nc = f->gmodular.num_channels, zz;
				int32_t nt = f->gmodular.nb_transforms;
				fwrite(&nc, 4, 1, pf); fwrite(&nt, 4, 1, pf);
				for (zz = 0; zz < nt; ++zz) {
					int32_t tr[6] = {0}; const microjxl__transform *t = &f->gmodular.transform[zz];
					tr[0] = t->tr;
					if (t->tr == MICROJXL__TR_SQUEEZE) { tr[1]=t->sq.horizontal; tr[2]=t->sq.in_place; tr[3]=t->sq.begin_c; tr[4]=t->sq.num_c; }
					else if (t->tr == MICROJXL__TR_PALETTE) { tr[1]=t->pal.begin_c; tr[2]=t->pal.num_c; tr[3]=t->pal.nb_colours; tr[4]=t->pal.nb_deltas; tr[5]=t->pal.d_pred; }
					fwrite(tr, 4, 6, pf);
				}
				for (zz = 0; zz < nc; ++zz) {
					microjxl__plane *pc = &f->gmodular.channel[zz];
					int32_t hdr[4] = {pc->width, pc->height, pc->hshift, pc->vshift};
					fwrite(hdr, 4, 4, pf);
					int32_t xx, yy;
					for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
						int32_t v = 0;
						if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx];
						else if (pc->type == MICROJXL__PLANE_I16) v = MICROJXL__I16_PIXELS(pc, yy)[xx];
						fwrite(&v, 4, 1, pf);
					}
				}
				fclose(pf);
			}
#endif
			MICROJXL__YIELD_AFTER(microjxl__inverse_transform(st, &f->gmodular));
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_DUMP_FPOST")) {
				static int fpostctr = 0;
				char pfn[64];
				snprintf(pfn, sizeof(pfn), "/tmp/mj_fpost_%d.bin", fpostctr++);
				FILE *pf = fopen(pfn, "wb");
				int32_t nc = f->gmodular.num_channels, zz;
				fwrite(&nc, 4, 1, pf);
				for (zz = 0; zz < nc; ++zz) {
					microjxl__plane *pc = &f->gmodular.channel[zz];
					int32_t hdr[4] = {pc->width, pc->height, pc->hshift, pc->vshift};
					fwrite(hdr, 4, 4, pf);
					int32_t xx, yy;
					for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
						int32_t v = 0;
						if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx];
						else if (pc->type == MICROJXL__PLANE_I16) v = MICROJXL__I16_PIXELS(pc, yy)[xx];
						fwrite(&v, 4, 1, pf);
					}
				}
				fclose(pf);
			}
#endif
#ifdef MICROJXL_DEBUG
			if (getenv("MICROJXL_DUMP_POST32")) {
				static int postctr = 0;
				char pfn[64];
				snprintf(pfn, sizeof(pfn), "/tmp/mj_postinv_%d.bin", postctr++);
				FILE *pf = fopen(pfn, "wb");
				int32_t nc = f->gmodular.num_channels, zz;
				fwrite(&nc, 4, 1, pf);
				for (zz = 0; zz < nc; ++zz) {
					microjxl__plane *pc = &f->gmodular.channel[zz];
					int32_t hdr[4] = {pc->width, pc->height, pc->hshift, pc->vshift};
					fwrite(hdr, 4, 4, pf);
					int32_t xx, yy;
					for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
						int32_t v = 0;
						if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx];
						else if (pc->type == MICROJXL__PLANE_I16) v = MICROJXL__I16_PIXELS(pc, yy)[xx];
						fwrite(&v, 4, 1, pf);
					}
				}
				fclose(pf);
			}
			fprintf(stderr, "[microjxl] post-inverse: %d channels\n", f->gmodular.num_channels);
			if (getenv("MICROJXL_DUMP_POST")) {
				FILE *pf = fopen("/tmp/microjxltest/postinv.bin", "wb");
				int32_t nc = f->gmodular.num_channels, zz;
				fwrite(&nc, 4, 1, pf);
				for (zz = 0; zz < nc; ++zz) {
					microjxl__plane *pc = &f->gmodular.channel[zz];
					int32_t hdr[4] = {pc->width, pc->height, pc->hshift, pc->vshift};
					fwrite(hdr, 4, 4, pf);
					if (pc->type == MICROJXL__PLANE_I16) {
						int yy;
						for (yy = 0; yy < pc->height; ++yy) fwrite(MICROJXL__I16_PIXELS(pc, yy), 2, (size_t) pc->width, pf);
					} else if (pc->type == MICROJXL__PLANE_I32) {
						int yy;
						for (yy = 0; yy < pc->height; ++yy) fwrite(MICROJXL__I32_PIXELS(pc, yy), 4, (size_t) pc->width, pf);
					} else {
						int32_t zero = 0;
						fwrite(&zero, 4, 1, pf);
					}
				}
				fclose(pf);
			}
			for (di = 0; di < f->gmodular.num_channels; ++di) {
				microjxl__plane *pc = &f->gmodular.channel[di];
				if (pc->type != MICROJXL__PLANE_EMPTY && pc->width > 0 && pc->height > 0) {
					int64_t sum = 0; int32_t mn = INT32_MAX, mx = INT32_MIN, xx, yy, n = 0;
					for (yy = 0; yy < pc->height; ++yy) for (xx = 0; xx < pc->width; ++xx) {
						int32_t v; n++;
						if (pc->type == MICROJXL__PLANE_I32) v = MICROJXL__I32_PIXELS(pc, yy)[xx]; else v = MICROJXL__I16_PIXELS(pc, yy)[xx];
						sum += v; if (v < mn) mn = v; if (v > mx) mx = v;
					}
					fprintf(stderr, "[microjxl] post ch%d %dx%d: min=%d max=%d mean=%.2f\n", di, pc->width, pc->height, mn, mx, n ? (double) sum / n : 0);
				}
			}
#endif
			if (!f->is_modular) MICROJXL__YIELD_AFTER(microjxl__combine_vardct(st, inner->lf_groups));

			/* modular patches apply pre-upsampling: the patch stage runs at
			 * stored resolution and patch coords index stored-resolution refs
			 * (libjxl stage_patches); the canvas is built from gmodular's
			 * stored-resolution planes, so this must precede upsample_frame */
			if (f->is_modular && f->has_patches) {
				MICROJXL__YIELD_AFTER(microjxl__apply_patches_modular(st));
				/* patches are now blended into gmodular; clear the dict so the
				 * per-pixel render path (XYB branch) does not apply them a
				 * second time (libjxl has a single patch stage) */
				f->patches.num_pos = 0;
			}
			/* modular restoration filters + splines run at stored resolution
			 * (libjxl stage order: gab/EPF/patches/splines pre-upsampling); 
			 * gaborish was previously missing for modular frames and EPF ran
			 * post-upsampling in the render */
			if (f->is_modular) {
				MICROJXL__YIELD_AFTER(microjxl__finalize_modular_frame(st));
			}
			/* VarDCT: frame-wide chroma upsampling, gaborish, EPF, patches,
			 * splines, noise and the colour conversion (libjxl's render
			 * pipeline is frame-wide; per-group filtering mirrors at seams). */
			if (!f->is_modular) {
				MICROJXL__YIELD_AFTER(microjxl__finalize_vardct_frame(st));
			}
			if (f->log_upsampling > 0) MICROJXL__YIELD_AFTER(microjxl__upsample_frame(st));
			/* VarDCT XYB: the combine deferred the nonlinear XYB->RGB stage
			 * (libjxl stage order: upsample the float XYB first); the floats
			 * travelled through ref_snap — convert them now at final
			 * resolution. */
			if (!f->is_modular && !f->do_ycbcr && f->log_upsampling > 0) {
				MICROJXL__YIELD_AFTER(microjxl__finalize_xyb_color(st));
			}

			/* save into the reference slot when referenced (libjxl FinalizeFrame:
			 * CanBeReferenced && save_as_reference != 0); REFONLY frames always
			 * qualify (not last, not LF, duration 0). Refs are saved at the
			 * final (upsampled) resolution, matching libjxl. */
			if (microjxl__frame_can_ref(f)) {
				/* libjxl dec_frame.cc FinalizeFrame: every CanBeReferenced frame
				 * is stored, at slot save_as_reference (slot 0 included — a
				 * duration-0 REFONLY frame with save_as_reference=0 still lands
				 * in slot 0 and patches may reference it). */
				MICROJXL__YIELD_AFTER(microjxl__save_ref_frame(st));
			}
			if (f->type == MICROJXL__FRAME_LF) {
				/* LF frame (spec LFFrame, libjxl "DC frame"): fully decoded
				 * above through the regular pipeline — the VarDCT combine or
				 * the modular inverse both leave the pre-colour-transform
				 * correlated XYB floats available for the capture (ref_snap
				 * / gmodular). Never displayed (K.5.2: only REGULAR||SKIPPROG
				 * frames are visible); continue with the next frame. */
				MICROJXL__YIELD_AFTER(microjxl__decode_lf_frame(st));
				inner->frame_state_live = 0; // decode_lf_frame freed the frame state
				if (inner->lf_groups) {
					int64_t li, nlg = inner->frame.num_lf_groups;
					for (li = 0; li < nlg; ++li) microjxl__mem_free_lf_group(&inner->lf_groups[li]);
					free(inner->lf_groups);
					inner->lf_groups = NULL;
				}
				microjxl__mem_free_toc(&inner->toc);
				memset(&inner->toc, 0, sizeof inner->toc);
				goto continue_frame_loop;
			}
			if (f->type == MICROJXL__FRAME_REFONLY) {
				microjxl__mem_free_frame_state(st->frame);
				microjxl__mem_free_toc(&inner->toc);
				memset(&inner->toc, 0, sizeof inner->toc);
				if (inner->lf_groups) {
					int64_t li;
					for (li = 0; li < inner->frame.num_lf_groups; ++li) {
						microjxl__mem_free_lf_group(&inner->lf_groups[li]);
					}
					free(inner->lf_groups);
					inner->lf_groups = NULL;
				}
				continue; // decode the next frame
			}
			/* JPEG reconstruction (Part 2 §9.10): after the final VarDCT frame
			 * of a jbrd-carrying container completed (capture state settled by
			 * dequant_hf/lf_quant hooks), assemble the reconstructed JPEG and
			 * cache it in the inner state (microjxl_jpeg_reconstruction hands
		 * it to the caller). Failure degrades to no reconstruction. */
			if (f->is_last && !f->is_modular && st->container->jbrd) {
				inner->jpeg_out = microjxl__jpeg_reconstruct_from_frame(st, &inner->jpeg_out_size);
			}
			break; // display frame (REGULAR or SKIPPROG)
			}

			/* end-of-image check: only after the final displayed frame; for
			 * non-last frames more sections/frames follow, so a here-position
			 * check would wrongly fail (excs) */
			if (f->is_last) {
				inner->image_done = 1;
				MICROJXL__YIELD_AFTER(microjxl__no_more_bytes(st));
			} else {
				MICROJXL__YIELD_AFTER(microjxl__zero_pad_to_byte(st));
				/* K.5.2: one frame is rendered per API call. Non-last display
				 * frames park here; the next `microjxl_next_frame` resumes the
				 * coroutine at `case MICROJXL__STATE_PARKED_FRAME` and the loop
				 * continues with the following frame header. */
				MICROJXL__PARK(MICROJXL__STATE_PARKED_FRAME);
			}
			break;
		}

	case MICROJXL__STATE_PARKED_FRAME: // resume here to decode the next frame
		if (inner->image_done) break; // no more frames
		if (inner->frame_state_live) {
			microjxl__mem_free_frame_state(st->frame);
			inner->frame_state_live = 0;
		}
		if (inner->lf_groups) {
			int64_t li, nlg = inner->frame.num_lf_groups;
			for (li = 0; li < nlg; ++li) microjxl__mem_free_lf_group(&inner->lf_groups[li]);
			free(inner->lf_groups);
			inner->lf_groups = NULL;
		}
		goto continue_frame_loop; // resume the `for(;;)` frame loop; frame_header reinits *f

	default: MICROJXL__UNREACHABLE();
	}

	return 0;
}

MICROJXL_STATIC void microjxl__mem_free_inner(microjxl__inner *inner) {
	int64_t i, num_lf_groups = inner->frame.num_lf_groups;
	microjxl__mem_free_source(&inner->source);
	microjxl__mem_free_container(&inner->container);
	microjxl__mem_free_buffer(&inner->buffer);
	microjxl__mem_free_image_state(&inner->image);
	microjxl__mem_free_frame_state(&inner->frame);
	if (inner->lf_groups) {
		for (i = 0; i < num_lf_groups; ++i) microjxl__mem_free_lf_group(&inner->lf_groups[i]);
		free(inner->lf_groups);
	}
	microjxl__mem_free_toc(&inner->toc);
	microjxl__mem_free_plane(&inner->rendered_rgba);
	microjxl__mem_free_plane(&inner->rendered_u8);
	microjxl__mem_free_plane(&inner->rendered_u16);
	{
		int pr, pc;
		for (pr = 0; pr < 3; ++pr) for (pc = 0; pc < 4; ++pc)
			microjxl__mem_free_plane(&inner->rendered_planar[pr][pc]);
	}
	microjxl__mem_free_plane(&inner->rendered_ec);
	inner->rendered_ec_idx = 0;
	if (inner->fb_ec && inner->fb_ec != inner->fb_ec_ptrs) microjxl__mem_free(inner->fb_ec);
	inner->fb_ec = NULL;
	inner->fb_ec_n = 0;
	microjxl__mem_free(inner->gen_icc);
	inner->gen_icc = NULL;
	inner->gen_icc_size = 0;
	microjxl__mem_free(inner->gen_out_icc);
	inner->gen_out_icc = NULL;
	inner->gen_out_icc_size = 0;
	microjxl__mem_free(inner->jpeg_out);
	inner->jpeg_out = NULL;
	inner->jpeg_out_size = 0;
	microjxl__mem_free(inner);
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// public API (implementation)

#ifdef MICROJXL_IMPLEMENTATION

MICROJXL_API microjxl_err microjxl_error(const microjxl_image *image) {
	microjxl__inner *inner; // ignored
	// do not alter image->magic even for Ufre
	return microjxl__check_image((microjxl_image *) (uintptr_t) image, MICROJXL__ORIGIN_NONE, &inner);
}

MICROJXL_API const char *microjxl_error_string(const microjxl_image *image) {
	static char static_errbuf[MICROJXL__ERRBUF_LEN];
	uint32_t origin = MICROJXL__ORIGIN_NONE;
	microjxl_err err = 0;
	const char *msg, *suffix;
	char *buf = NULL;
	int saved_errno = 0;
	int32_t i, corrupted_image = 0;

	if (!image) {
		snprintf(static_errbuf, MICROJXL__ERRBUF_LEN, "`image` parameter is NULL during microjxl_error_string");
		return static_errbuf;
	}
	if (image->magic == MICROJXL__IMAGE_MAGIC) {
		if (image->u.inner && image->u.inner->magic == MICROJXL__INNER_MAGIC) {
			origin = image->u.inner->origin;
			err = image->u.inner->err;
			buf = image->u.inner->errbuf;
			saved_errno = image->u.inner->saved_errno;
		} else {
			corrupted_image = 1;
		}
	} else {
		origin = image->magic ^ MICROJXL__IMAGE_ERR_MAGIC;
		if (0 < origin && origin <= MICROJXL__ORIGIN_LAST_ALT_MAGIC) {
			err = image->u.err;
			buf = static_errbuf;
			saved_errno = 0;
			// do not alter image->magic even for Ufre, but the message will be altered accordingly
			if (origin == MICROJXL__ORIGIN_NEXT) origin = MICROJXL__ORIGIN_error_string;
		} else {
			origin = image->magic ^ MICROJXL__IMAGE_OPEN_ERR_MAGIC;
			if (0 < origin && origin <= MICROJXL__ORIGIN_LAST_ALT_MAGIC) {
				err = MICROJXL__4("open");
				buf = static_errbuf;
				saved_errno = image->u.saved_errno;
			} else {
				corrupted_image = 1;
			}
		}
	}
	if (corrupted_image) {
		snprintf(static_errbuf, MICROJXL__ERRBUF_LEN,
			"`image` parameter is found corrupted during microjxl_error_string");
		return static_errbuf;
	}

	// TODO acquire a spinlock for buf if threaded

	msg = NULL;
	suffix = "";
	for (i = 0; i < (int32_t) (sizeof(MICROJXL__ERROR_STRINGS) / sizeof(*MICROJXL__ERROR_STRINGS)); ++i) {
		if (err == MICROJXL__4(MICROJXL__ERROR_STRINGS[i].err)) {
			msg = MICROJXL__ERROR_STRINGS[i].msg;
			if (MICROJXL__ERROR_STRINGS[i].suffix) suffix = MICROJXL__ERROR_STRINGS[i].suffix;
			break;
		}
	}
	if (!msg) {
		snprintf(buf, MICROJXL__ERRBUF_LEN, "Decoding failed (%c%c%c%c) during microjxl_%s",
			err >> 24 & 0xff, err >> 16 & 0xff, err >> 8 & 0xff, err & 0xff, MICROJXL__ORIGIN_NAMES[origin]);
	} else if (saved_errno) {
		snprintf(buf, MICROJXL__ERRBUF_LEN, "%s during microjxl_%s%s: %s",
			msg, MICROJXL__ORIGIN_NAMES[origin], suffix, strerror(saved_errno));
	} else {
		snprintf(buf, MICROJXL__ERRBUF_LEN, "%s during microjxl_%s%s", msg, MICROJXL__ORIGIN_NAMES[origin], suffix);
	}
	return buf;
}

MICROJXL_API microjxl_err microjxl_from_memory(microjxl_image *image, void *buf, size_t size, microjxl_memory_free_func freefunc) {
	static const microjxl__origin ORIGIN = MICROJXL__ORIGIN_from_memory;
	microjxl__inner *inner;
	microjxl__st stbuf, *st = &stbuf;

	if (!image) return MICROJXL__4("Uim0");
	if (!buf) return microjxl__set_alt_magic(MICROJXL__4("Ubf0"), 0, ORIGIN, image);

	inner = (microjxl__inner*) microjxl__calloc(1, sizeof(microjxl__inner));
	if (!inner) return microjxl__set_alt_magic(MICROJXL__4("!mem"), 0, ORIGIN, image);

	microjxl__init_state(st, inner);
	if (microjxl__init_memory_source(st, (uint8_t*) buf, size, freefunc, &inner->source)) {
		microjxl__mem_free_inner(inner);
		return microjxl__set_alt_magic(st->err, st->saved_errno, ORIGIN, image);
	} else {
		MICROJXL__ASSERT(!st->err);
		return microjxl__set_magic(inner, image);
	}
}

MICROJXL_API microjxl_err microjxl_from_file(microjxl_image *image, const char *path) {
	static const microjxl__origin ORIGIN = MICROJXL__ORIGIN_from_file;
	microjxl__inner *inner;
	microjxl__st stbuf, *st = &stbuf;

	if (!image) return MICROJXL__4("Uim0");
	if (!path) return microjxl__set_alt_magic(MICROJXL__4("Upt0"), 0, ORIGIN, image);

	inner = (microjxl__inner*) microjxl__calloc(1, sizeof(microjxl__inner));
	if (!inner) return microjxl__set_alt_magic(MICROJXL__4("!mem"), 0, ORIGIN, image);

	microjxl__init_state(st, inner);
	if (microjxl__init_file_source(st, path, &inner->source)) {
		microjxl__mem_free_inner(inner);
		return microjxl__set_alt_magic(st->err, st->saved_errno, ORIGIN, image);
	} else {
		MICROJXL__ASSERT(!st->err);
		return microjxl__set_magic(inner, image);
	}
}

MICROJXL_API microjxl_err microjxl_output_format(microjxl_image *image, int32_t channel, int32_t format) {
	static const microjxl__origin ORIGIN = MICROJXL__ORIGIN_output_format;
	microjxl__inner *inner;

	MICROJXL__CHECK_IMAGE();

	if (channel != MICROJXL_RGBA &&
		    channel != MICROJXL_RED && channel != MICROJXL_GREEN &&
		    channel != MICROJXL_BLUE && channel != MICROJXL_ALPHA)
		return MICROJXL__SET_INNER_ERR("Uch?");
	if (channel == MICROJXL_RGBA) {
		// interleaved RGBA: 4 samples per pixel
		if (format != MICROJXL_U8X4 && format != MICROJXL_U16X4 && format != MICROJXL_F32X4)
			return MICROJXL__SET_INNER_ERR("Ufm?");
	} else {
		// planar single channel: 1 sample per pixel
		if (format != MICROJXL_U8 && format != MICROJXL_U16 && format != MICROJXL_F32)
			return MICROJXL__SET_INNER_ERR("Ufm?");
	}

	return 0;
}

MICROJXL_API microjxl_err microjxl_set_coalescing(microjxl_image *image, int coalescing) {
	static const microjxl__origin ORIGIN = MICROJXL__ORIGIN_output_format;
	microjxl__inner *inner;

	MICROJXL__CHECK_IMAGE();

	/* libjxl requires this before decoding starts; once a frame has been
	 * rendered the mode is locked (mid-stream switches would produce an
	 * inconsistent canvas). */
	if (inner->rendered) return MICROJXL__SET_INNER_ERR("Uco?");
	inner->coalescing = (coalescing != 0);
	return 0;
}

MICROJXL_API microjxl_err microjxl_set_render_spot(microjxl_image *image, int render_spot) {
	static const microjxl__origin ORIGIN = MICROJXL__ORIGIN_set_render_spot;
	microjxl__inner *inner;
	microjxl_err err;

	err = microjxl__check_image(image, ORIGIN, &inner);
	if (err) return err;
	if (inner->rendered) return MICROJXL__SET_INNER_ERR("Uco?");
	inner->image.render_spot = (render_spot != 0);
	/* survives the lazy image_metadata() reset on the first next_frame */
	inner->render_spot_pending = (render_spot != 0);
	return 0;
}

MICROJXL_API int microjxl_next_frame(microjxl_image *image) {
	static const microjxl__origin ORIGIN = MICROJXL__ORIGIN_next_frame;
	microjxl__inner *inner;
	microjxl__st stbuf;
	microjxl_err err;
	int32_t i2;

	err = microjxl__check_image(image, ORIGIN, &inner);
	if (err) return 0; // does NOT return err!

	if (inner->rendered) {
		/* A frame is already on screen: drop it and decode the next one
		 * (JPEG XL animations emit one visible frame per call). `advance`
		 * is resumable: it parks after each non-last display frame and
		 * only completes when the last frame has been decoded. */
		microjxl__mem_free_plane(&inner->rendered_rgba);
		microjxl__mem_free_plane(&inner->rendered_u8);
		microjxl__mem_free_plane(&inner->rendered_u16);
		{
			int pr, pc;
			for (pr = 0; pr < 3; ++pr) for (pc = 0; pc < 4; ++pc)
				microjxl__mem_free_plane(&inner->rendered_planar[pr][pc]);
		}
		microjxl__mem_free_plane(&inner->rendered_ec);
		inner->rendered_ec_idx = -1;
		inner->rendered = 0;
		if (inner->image_done) return 0; // image fully consumed
	}
	/* Decode frames until one can be handed out (libjxl is_last_of_still:
	 * coalesced mode exposes visible frames only — is_last or duration > 0;
	 * non-coalesced mode also exposes zero-duration non-last REGULAR
	 * frames as individual layers). The loop body below processes each
	 * decoded frame (render + canvas/ref update) and either exposes it
	 * or — for non-visible frames in coalesced mode, whose pixels were
	 * merged into the canvas — continues with the next frame. */
	for (;;) {
		err = microjxl__advance(inner, ORIGIN);
		if (err) return 0;
		/* `frame_state_live` means advance() decoded a display frame that is
		 * waiting to be handed out (parked mid-image, or the last frame). */
		if (!inner->frame_state_live) return 0;

		/* capture the geometry/metadata of the frame being processed
		 * (libjxl non-coalesced GetFrameSize reports the frame's own
		 * upsampled dimensions; coalesced reports the canvas) */
		if (inner->coalescing) {
			inner->frame_x0 = 0;
			inner->frame_y0 = 0;
			inner->frame_width = inner->image.width;
			inner->frame_height = inner->image.height;
		} else {
			inner->frame_x0 = inner->frame.x0;
			inner->frame_y0 = inner->frame.y0;
			inner->frame_width = inner->frame.width;
			inner->frame_height = inner->frame.height;
		}
		inner->frame_duration = inner->frame.duration;
		inner->frame_is_last = inner->frame.is_last;

		microjxl__init_state(&stbuf, inner);
		/* release the previous frame's pre-blend EC-alpha snapshot (freed
		 * here so every frame path — blended or not — is covered) */
		if (inner->ec_alpha_pre) {
			microjxl__mem_free_plane(inner->ec_alpha_pre);
			microjxl__mem_free(inner->ec_alpha_pre);
			inner->ec_alpha_pre = NULL;
		}
		/* Coalesced EC canvas (libjxl dec_state->full_image EC planes):
		 * composite the frame's own EC planes onto the canvas-side EC
		 * planes BEFORE the render — render_rgba_f32 consumes/frees the
		 * frame's modular channels, so their planes are only valid here. */
		if (inner->image.num_extra_channels > 0) {
			int32_t cc = (inner->frame.gmodular.num_channels >= 3) ? 3 : 1;
			int32_t n_fe = inner->frame.gmodular.num_channels > cc
				? inner->frame.gmodular.num_channels - cc : 0;
			/* the helper takes an array of plane POINTERS, one per EC */
			microjxl__plane *fe_ptrs[8];
			microjxl__plane **fe_arr = fe_ptrs;
			if (n_fe > (int32_t) (sizeof(fe_ptrs) / sizeof(fe_ptrs[0]))) {
				fe_arr = (microjxl__plane **) microjxl__malloc((size_t) n_fe, sizeof(microjxl__plane *));
				if (!fe_arr) { inner->origin = ORIGIN; inner->err = MICROJXL__4("!mem"); return 0; }
			}
			for (i2 = 0; i2 < n_fe; ++i2) fe_arr[i2] = &inner->frame.gmodular.channel[cc + i2];
			/* libjxl PerformBlending: "Blend extra channels first so that we
			 * use the pre-blending alpha." The EC canvas is UPDATED by the EC
			 * blend below, so the colour blend must read the PRE-FRAME canvas
			 * alpha: snapshot it before frame_ec_blend_canvas runs (NULL rows
			 * = no canvas yet). frame_blend_canvas unrefs it at the end. */
			if (microjxl__frame_needs_blending(&inner->frame, inner->image.num_extra_channels,
				                               inner->image.width, inner->image.height) &&
			    inner->image.num_ec_canvas > 0) {
				int has_alpha_ec = 0, a_i;
				for (a_i = 0; a_i < inner->image.num_extra_channels; ++a_i) {
					if (inner->image.ec_info[a_i].type == MICROJXL__EC_ALPHA) { has_alpha_ec = 1; break; }
				}
				if (has_alpha_ec && inner->image.ec_canvas[a_i].type == MICROJXL__PLANE_F32) {
					microjxl__plane *snap = (microjxl__plane *) microjxl__malloc(1, sizeof(microjxl__plane));
					if (snap) {
						memset(snap, 0, sizeof(*snap));
						microjxl__init_empty_plane(snap);
						if (inner->image.ec_canvas[a_i].width == inner->image.width &&
						    inner->image.ec_canvas[a_i].height == inner->image.height) {
							if (!microjxl__init_plane(&stbuf, MICROJXL__PLANE_F32, inner->image.width, inner->image.height,
								MICROJXL__PLANE_FORCE_PAD, snap)) {
								int sy;
								for (sy = 0; sy < inner->image.height; ++sy)
									memcpy(MICROJXL__F32_PIXELS(snap, sy),
									       MICROJXL__F32_PIXELS(&inner->image.ec_canvas[a_i], sy),
									       (size_t) inner->image.width * sizeof(float));
								inner->ec_alpha_pre = snap;
							} else {
								microjxl__mem_free_plane(snap);
								microjxl__mem_free(snap);
							}
						} else {
							microjxl__mem_free_plane(snap);
							microjxl__mem_free(snap);
						}
					}
				}
			}
			err = microjxl__frame_ec_blend_canvas(&stbuf, fe_arr, n_fe, inner->image.num_extra_channels);
			if (err) {
				if (fe_arr != fe_ptrs) microjxl__mem_free(fe_arr);
				inner->origin = ORIGIN;
				inner->err = err;
				return 0;
			}
			/* keep the frame's own EC planes for the canvas colour blend's
			 * fg alpha (render_rgba_f32 may consume the gmodular channels) */
			if (n_fe > (int32_t) (sizeof(inner->fb_ec_ptrs) / sizeof(inner->fb_ec_ptrs[0]))) {
				inner->fb_ec = (microjxl__plane **) microjxl__malloc((size_t) n_fe, sizeof(microjxl__plane *));
				if (!inner->fb_ec) {
					if (fe_arr != fe_ptrs) microjxl__mem_free(fe_arr);
					inner->origin = ORIGIN; inner->err = MICROJXL__4("!mem"); return 0;
				}
			} else {
				inner->fb_ec = inner->fb_ec_ptrs;
			}
			for (i2 = 0; i2 < n_fe; ++i2) inner->fb_ec[i2] = fe_arr[i2];
			inner->fb_ec_n = n_fe;
			if (fe_arr != fe_ptrs) microjxl__mem_free(fe_arr);
		}
		err = microjxl__render_rgba_f32(&stbuf, &inner->rendered_rgba);
	if (err) {
		inner->origin = ORIGIN;
		inner->err = err;
		return 0;
	}
	/* K.5.2 frame blending (libjxl stage_blending): when the displayed
	 * frame is not a full-canvas REPLACE, composite it onto the canvas
	 * (the previous display frame's blended output, or opaque black);
	 * the canvas then becomes the displayed image and is carried over
	 * to the following frames. Display-referred domain: the blending
	 * happens after colour transform (libjxl places the blending stage
	 * after the XYB stage for blending frames). */
	if (microjxl__frame_needs_blending(&inner->frame, inner->image.num_extra_channels, inner->image.width, inner->image.height)) {
		microjxl__plane blended = MICROJXL__INIT;
		err = microjxl__frame_blend_canvas(&stbuf, &inner->rendered_rgba, &blended,
			inner->fb_ec, inner->fb_ec_n,
			inner->image.num_ec_canvas > 0 ? inner->image.ec_canvas : NULL,
			inner->ec_alpha_pre);
		if (!err && getenv("MICROJXL_DUMP_BLEND")) {
			FILE *df;
			int32_t dy;
			df = fopen("/tmp/mj_fg.bin", "wb");
			if (df) {
				for (dy = 0; dy < inner->frame.height; ++dy)
					fwrite(MICROJXL__F32_PIXELS(&inner->rendered_rgba, dy), sizeof(float), (size_t) inner->frame.width * 4, df);
				fclose(df);
			}
			df = fopen("/tmp/mj_bg.bin", "wb");
			if (df && inner->image.ref_render) {
				for (dy = 0; dy < inner->image.height; ++dy)
					fwrite(MICROJXL__F32_PIXELS(inner->image.ref_render, dy), sizeof(float), (size_t) inner->image.width * 4, df);
				fclose(df);
			}
			df = fopen("/tmp/mj_blended.bin", "wb");
			if (df) {
				for (dy = 0; dy < inner->image.height; ++dy)
					fwrite(MICROJXL__F32_PIXELS(&blended, dy), sizeof(float), (size_t) inner->image.width * 4, df);
				fclose(df);
			}
			fprintf(stderr, "[microjxl] blend dump: fg=%dx%d at %d,%d bg=%p\n", inner->frame.width, inner->frame.height,
				(int) inner->frame.x0, (int) inner->frame.y0, (void *) inner->image.ref_render);
		}
		if (!err) {
			/* the blended result becomes the displayed image (coalesced) and
			 * is stored into reference slot save_as_ref when the frame can be
			 * referenced (libjxl WriteToImageBundleStage sits AFTER blending;
			 * FinalizeFrame stores it into reference_frames[save_as_ref]).
			 * Slots persist across frames; blending reads the SOURCE slot. */
			if (!inner->image.canvas) {
				microjxl__plane *cptr = (microjxl__plane *) microjxl__malloc(1, sizeof(microjxl__plane));
				if (cptr) {
					memset(cptr, 0, sizeof(*cptr));
					inner->image.canvas = cptr;
				}
			}
			if (inner->image.canvas) {
				microjxl__mem_free_plane(inner->image.canvas);
				*inner->image.canvas = blended;
				memset(&blended, 0, sizeof(blended));
				/* display the blended canvas (coalesced mode only: the
				 * non-coalesced mode exposes the individual unblended
				 * layer, while the canvas still advances for subsequent
				 * frames) */
				if (inner->coalescing) {
					microjxl__mem_free_plane(&inner->rendered_rgba);
					err = microjxl__init_plane(&stbuf, MICROJXL__PLANE_F32, inner->image.width * 4, inner->image.height, MICROJXL__PLANE_FORCE_PAD, &inner->rendered_rgba);
					if (!err) {
						int32_t cy;
						for (cy = 0; cy < inner->image.height; ++cy) {
							memcpy(MICROJXL__F32_PIXELS(&inner->rendered_rgba, cy),
								MICROJXL__F32_PIXELS(inner->image.canvas, cy),
								(size_t) inner->image.width * 4 * sizeof(float));
							}
						}
					}
				} else {
					err = microjxl__set_error(&stbuf, MICROJXL__4("!mem"));
				}
				/* store the post-blend display render into the reference slot */
				if (!err && microjxl__frame_can_ref(&inner->frame)) {
					err = microjxl__store_blend_ref(&stbuf, inner->image.canvas);
				}
				if (err) {
					microjxl__mem_free_plane(&blended);
					inner->origin = ORIGIN;
					inner->err = err;
					return 0;
				}
			} else {
				microjxl__mem_free_plane(&blended);
				err = microjxl__set_error(&stbuf, MICROJXL__4("!mem"));
				inner->origin = ORIGIN;
				inner->err = err;
				return 0;
			}
		} else if (microjxl__frame_can_ref(&inner->frame)) {
			/* a full-canvas REPLACE frame that does not need blending: the
			 * render itself IS the post-blend result (WriteToImageBundle
			 * still runs in libjxl's pipeline for these frames); store it
			 * (slot save_as_ref, slot 0 included for duration-0 frames). */
			err = microjxl__store_blend_ref(&stbuf, &inner->rendered_rgba);
			if (err) {
				inner->origin = ORIGIN;
				inner->err = err;
				return 0;
			}
		}
		if (inner->ec_alpha_pre) { /* no blend path: release the snapshot */
			microjxl__mem_free_plane(inner->ec_alpha_pre);
			microjxl__mem_free(inner->ec_alpha_pre);
			inner->ec_alpha_pre = NULL;
		}
	/* Exposure gate (libjxl is_last_of_still, decode.cc ~1350): in coalesced
	 * mode only visible frames (is_last || duration > 0) are handed out;
	 * an invisible frame's pixels already merged into the canvas above, so
	 * continue with the next frame (the parked coroutine frees the frame
	 * state on resume, so frame_state_live must stay set here).
	 * Non-coalesced mode exposes every REGULAR/SKIPPROG frame as its
	 * individual layer. */
	if (inner->coalescing &&
			!(inner->frame.is_last || inner->frame.duration > 0)) {
		/* drop the rendered plane: the canvas/ref_render carry the merged
		 * result; the next loop iteration reuses this frame slot */
		microjxl__mem_free_plane(&inner->rendered_rgba);
		continue;
	}
	inner->rendered = 1;
	return 1;
	}
}

MICROJXL_API microjxl_frame microjxl_current_frame(microjxl_image *image) {
	static const microjxl__origin ORIGIN = MICROJXL__ORIGIN_current_frame;
	microjxl__inner *inner;
	microjxl_frame frame;
	microjxl_err err;

	err = microjxl__check_image(image, ORIGIN, &inner);
	frame.magic = MICROJXL__FRAME_ERR_MAGIC;
	frame.reserved = 0;
	frame.inner = inner;
	if (err) return frame;

	if (!inner->rendered) {
		if (!microjxl_next_frame(image)) { // if microjxl_next_frame hasn't been called, implicity call it
			if (inner->err) return frame; // at this point we are sure that inner exists
		}
	}

	frame.magic = MICROJXL__FRAME_MAGIC;
	return frame;
}

/* The master render is float [0,1] (microjxl__render_rgba_f32); the u8/u16
 * accessors convert on demand and cache the result for the lifetime of
 * the current frame (invalidated when the plane is freed by next_frame).
 * u8 conversion keeps the historical integer-rescaling result exactly:
 * v * 255 + 0.5 rounds to the same integer as v * maxpixel + 0.5 for
 * every integer v/maxpixel with maxpixel <= 65535. */
/* Extracts channel `c` (0..3) of the master RGBA f32 render into a
 * single-channel plane of the given type. The master plane stores RGBA
 * interleaved with its `width` counting *samples* (4 per pixel), so the
 * output plane is one sample per pixel (width = src->width / 4).
 * `dst` is a cache slot owned by the caller (invalidated with the other
 * render caches). Quantization matches microjxl__convert_f32_to exactly
 * (round-half-up + clamp), so a planar read is bit-identical to the
 * interleaved one. */
MICROJXL_STATIC void microjxl__extract_channel_f32(
	microjxl__st *st, const microjxl__plane *src, microjxl__plane *dst, uint8_t type, int32_t c
) {
	int32_t y, x;
	int32_t width = src->width / 4;
	if (dst->type == type && dst->width == width && dst->height == src->height) return; // cached
	if (dst->type != type) {
		microjxl__mem_free_plane(dst);
		microjxl__init_empty_plane(dst);
	}
	if (dst->width != width || dst->height != src->height) {
		microjxl__init_empty_plane(dst);
		MICROJXL__TRY(microjxl__init_plane(st, type, width, src->height, MICROJXL__PLANE_FORCE_PAD, dst));
	}
	if (type == MICROJXL__PLANE_U8) {
		for (y = 0; y < src->height; ++y) {
			const float *s = MICROJXL__F32_PIXELS(src, y) + c;
			uint8_t *d = MICROJXL__U8_PIXELS(dst, y);
			for (x = 0; x < width; ++x) {
				float v = s[x * 4];
				int32_t q = (int32_t) (v * 255.0f + 0.5f);
				d[x] = (uint8_t) (q < 0 ? 0 : q > 255 ? 255 : q);
			}
		}
	} else if (type == MICROJXL__PLANE_U16) {
		for (y = 0; y < src->height; ++y) {
			const float *s = MICROJXL__F32_PIXELS(src, y) + c;
			uint16_t *d = MICROJXL__U16_PIXELS(dst, y);
			for (x = 0; x < width; ++x) {
				float v = s[x * 4];
				int32_t q = (int32_t) (v * 65535.0f + 0.5f);
				d[x] = (uint16_t) (q < 0 ? 0 : q > 65535 ? 65535 : q);
			}
		}
	} else { // F32
		for (y = 0; y < src->height; ++y) {
			const float *s = MICROJXL__F32_PIXELS(src, y) + c;
			float *d = MICROJXL__F32_PIXELS(dst, y);
			for (x = 0; x < width; ++x) d[x] = s[x * 4];
		}
	}
	return;

MICROJXL__ON_ERROR:
	microjxl__mem_free_plane(dst);
	microjxl__init_empty_plane(dst);
}

MICROJXL_STATIC void microjxl__convert_f32_to(microjxl__st *st, const microjxl__plane *src, microjxl__plane *dst, uint8_t type) {
	int32_t y, x;
	if (dst->type == type && dst->width == src->width && dst->height == src->height) return; // cached
	if (dst->type != type) {
		microjxl__mem_free_plane(dst);
		microjxl__init_empty_plane(dst);
	}
	if (dst->width != src->width || dst->height != src->height) {
		microjxl__init_empty_plane(dst);
		MICROJXL__TRY(microjxl__init_plane(st, type, src->width, src->height, MICROJXL__PLANE_FORCE_PAD, dst));
	}
	if (type == MICROJXL__PLANE_U8) {
		for (y = 0; y < src->height; ++y) {
			const float *s = MICROJXL__F32_PIXELS(src, y);
			uint8_t *d = MICROJXL__U8_PIXELS(dst, y);
			for (x = 0; x < src->width; ++x) {
				float v = s[x];
				int32_t q = (int32_t) (v * 255.0f + 0.5f);
				d[x] = (uint8_t) (q < 0 ? 0 : q > 255 ? 255 : q);
			}
		}
	} else {
		for (y = 0; y < src->height; ++y) {
			const float *s = MICROJXL__F32_PIXELS(src, y);
			uint16_t *d = MICROJXL__U16_PIXELS(dst, y);
			for (x = 0; x < src->width; ++x) {
				float v = s[x];
				int32_t q = (int32_t) (v * 65535.0f + 0.5f);
				d[x] = (uint16_t) (q < 0 ? 0 : q > 65535 ? 65535 : q);
			}
		}
	}
	return;

MICROJXL__ON_ERROR:
	microjxl__init_empty_plane(dst);
}

/* Shared accessor body: `dst` receives (and caches) the converted plane.
 * The cache is invalidated by next_frame/free (which free these planes);
 * it always belongs to the currently rendered frame. */
static microjxl__plane *microjxl__frame_rendered_plane(
	const microjxl_frame *frame, int32_t channel, uint8_t type, microjxl__plane *dst
) {
	microjxl__st stbuf;
	microjxl__inner *inner;
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC) return NULL;
	inner = frame->inner;
	if (!inner || inner->magic != MICROJXL__INNER_MAGIC) return NULL;
	if (channel != MICROJXL_RGBA) return NULL;
	if (!inner->rendered || inner->rendered_rgba.type != MICROJXL__PLANE_F32) return NULL;
	if (type == MICROJXL__PLANE_F32) return &inner->rendered_rgba;
	microjxl__init_state(&stbuf, inner);
	microjxl__convert_f32_to(&stbuf, &inner->rendered_rgba, dst, type);
	if (stbuf.err) {
		inner->origin = MICROJXL__ORIGIN_frame_pixels;
		inner->err = stbuf.err;
		return NULL;
	}
	return dst;
}

/* Planar variant: extracts channel `c` (0..3) of the RGBA render into a
 * single-channel plane of `type`; `cache` is the caller-owned
 * per-(type,c) slot (invalidated with the other render caches). */
static microjxl__plane *microjxl__frame_rendered_plane_planar(
	const microjxl_frame *frame, int32_t channel, uint8_t type, microjxl__plane *cache
) {
	microjxl__st stbuf;
	microjxl__inner *inner;
	int32_t c;
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC) return NULL;
	inner = frame->inner;
	if (!inner || inner->magic != MICROJXL__INNER_MAGIC) return NULL;
	switch (channel) { // channel selector: colour channels + alpha only
		case MICROJXL_RED: c = 0; break;
		case MICROJXL_GREEN: c = 1; break;
		case MICROJXL_BLUE: c = 2; break;
		case MICROJXL_ALPHA: c = 3; break;
		default: return NULL;
	}
	if (!inner->rendered || inner->rendered_rgba.type != MICROJXL__PLANE_F32) return NULL;
	microjxl__init_state(&stbuf, inner);
	microjxl__extract_channel_f32(&stbuf, &inner->rendered_rgba, cache, type, c);
	if (stbuf.err) {
		inner->origin = MICROJXL__ORIGIN_frame_pixels;
		inner->err = stbuf.err;
		return NULL;
	}
	return cache;
}

MICROJXL_API microjxl_pixels_u8x4 microjxl_frame_pixels_u8x4(const microjxl_frame *frame, int32_t channel) {
	static const microjxl__origin ORIGIN = MICROJXL__ORIGIN_frame_pixels;

	// on error, return this placeholder image (TODO should this include an error message?)
	#define MICROJXL__U8X4_THIRD(a,b,c,d,e,f,g) 255,0,0,a*255, 255,0,0,b*255, 255,0,0,c*255, \
		255,0,0,d*255, 255,0,0,e*255, 255,0,0,f*255, 255,0,0,g*255
	#define MICROJXL__U8X4_ROW(aa,bb,cc) MICROJXL__U8X4_THIRD aa, MICROJXL__U8X4_THIRD bb, MICROJXL__U8X4_THIRD cc
	static const uint8_t ERROR_PIXELS_DATA[] = {
		MICROJXL__U8X4_ROW((1,1,1,1,1,1,1),(1,1,1,1,1,1,1),(1,1,1,1,1,1,1)),
		MICROJXL__U8X4_ROW((1,0,0,0,1,1,1),(1,1,1,1,1,1,1),(1,1,1,1,1,1,1)),
		MICROJXL__U8X4_ROW((1,0,1,1,1,1,1),(1,1,1,1,1,1,1),(1,1,1,1,1,1,1)),
		MICROJXL__U8X4_ROW((1,0,0,0,1,0,0),(0,1,0,0,0,1,0),(0,0,1,0,0,0,1)),
		MICROJXL__U8X4_ROW((1,0,1,1,1,0,1),(1,1,0,1,1,1,0),(1,0,1,0,1,1,1)),
		MICROJXL__U8X4_ROW((1,0,0,0,1,0,1),(1,1,0,1,1,1,0),(0,0,1,0,1,1,1)),
		MICROJXL__U8X4_ROW((1,1,1,1,1,1,1),(1,1,1,1,1,1,1),(1,1,1,1,1,1,1)),
	};
	static const microjxl_pixels_u8x4 ERROR_PIXELS = {21, 7, 21 * 4, ERROR_PIXELS_DATA};

	microjxl__inner *inner;
	microjxl_pixels_u8x4 pixels;

	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC) return ERROR_PIXELS;
	inner = frame->inner;
	if (!inner || inner->magic != MICROJXL__INNER_MAGIC) return ERROR_PIXELS;

	// TODO support more channels
	if (channel != MICROJXL_RGBA) return ERROR_PIXELS;

	// TODO this condition is impossible under the current API
	if (!inner->rendered) return MICROJXL__SET_INNER_ERR("Urnd"), ERROR_PIXELS;

	{
		microjxl__plane *p = microjxl__frame_rendered_plane(frame, channel, MICROJXL__PLANE_U8, &inner->rendered_u8);
		if (!p) return ERROR_PIXELS;
		MICROJXL__ASSERT(p->width % 4 == 0);
		pixels.width = p->width / 4;
		pixels.height = p->height;
		pixels.stride_bytes = p->stride_bytes;
		pixels.data = (void*) p->pixels;
	}
	return pixels;
}

MICROJXL_API const microjxl_u8x4 *microjxl_row_u8x4(microjxl_pixels_u8x4 pixels, int32_t y) {
	MICROJXL__ASSERT(0 <= y && y < pixels.height);
	MICROJXL__ASSERT(pixels.stride_bytes > 0);
	MICROJXL__ASSERT(pixels.data);
	return (const microjxl_u8x4*) (uintptr_t) ((const char*) pixels.data + (size_t) pixels.stride_bytes * (size_t) y);
}

MICROJXL_API microjxl_pixels_u16x4 microjxl_frame_pixels_u16x4(const microjxl_frame *frame, int32_t channel) {
	// on error, return an empty placeholder (callers must check width/height)
	static const microjxl_pixels_u16x4 ERROR_PIXELS = {0, 0, 0, NULL};
	microjxl__plane *p = microjxl__frame_rendered_plane(frame, channel, MICROJXL__PLANE_U16, frame ? (frame->inner ? &frame->inner->rendered_u16 : NULL) : NULL);
	microjxl_pixels_u16x4 pixels;
	if (!p) return ERROR_PIXELS;
	MICROJXL__ASSERT(p->width % 4 == 0);
	pixels.width = p->width / 4;
	pixels.height = p->height;
	pixels.stride_bytes = p->stride_bytes;
	pixels.data = (void*) p->pixels;
	return pixels;
}

MICROJXL_API const microjxl_u16x4 *microjxl_row_u16x4(microjxl_pixels_u16x4 pixels, int32_t y) {
	MICROJXL__ASSERT(0 <= y && y < pixels.height);
	MICROJXL__ASSERT(pixels.stride_bytes > 0);
	MICROJXL__ASSERT(pixels.data);
	return (const microjxl_u16x4*) (uintptr_t) ((const char*) pixels.data + (size_t) pixels.stride_bytes * (size_t) y);
}

MICROJXL_API microjxl_pixels_f32x4 microjxl_frame_pixels_f32x4(const microjxl_frame *frame, int32_t channel) {
	static const microjxl_pixels_f32x4 ERROR_PIXELS = {0, 0, 0, NULL};
	microjxl__plane *p = microjxl__frame_rendered_plane(frame, channel, MICROJXL__PLANE_F32, NULL);
	microjxl_pixels_f32x4 pixels;
	if (!p) return ERROR_PIXELS;
	MICROJXL__ASSERT(p->width % 4 == 0);
	pixels.width = p->width / 4;
	pixels.height = p->height;
	pixels.stride_bytes = p->stride_bytes;
	pixels.data = (void*) p->pixels;
	return pixels;
}

MICROJXL_API const microjxl_f32x4 *microjxl_row_f32x4(microjxl_pixels_f32x4 pixels, int32_t y) {
	MICROJXL__ASSERT(0 <= y && y < pixels.height);
	MICROJXL__ASSERT(pixels.stride_bytes > 0);
	MICROJXL__ASSERT(pixels.data);
	return (const microjxl_f32x4*) (uintptr_t) ((const char*) pixels.data + (size_t) pixels.stride_bytes * (size_t) y);
}

////////////////////////////////////////////////////////////////////////////////
// planar (single-channel) accessors: extract one channel (RED/GREEN/BLUE/
// ALPHA) of the current frame's RGBA render. The master render is RGBA; a
// missing alpha channel is opaque (1.0), so ALPHA always decodes. Quantized
// values match the interleaved accessors bit-for-bit.

static microjxl__plane *microjxl__planar_cache_slot(microjxl__inner *inner, uint8_t type, int32_t channel) {
	int row = type == MICROJXL__PLANE_U8 ? 0 : type == MICROJXL__PLANE_U16 ? 1 : 2;
	int col = channel == MICROJXL_RED ? 0 : channel == MICROJXL_GREEN ? 1 : channel == MICROJXL_BLUE ? 2 : 3;
	return &inner->rendered_planar[row][col];
}

MICROJXL_API microjxl_pixels_u8 microjxl_frame_pixels_u8(const microjxl_frame *frame, int32_t channel) {
	// on error, return an empty placeholder (callers must check width/height)
	static const microjxl_pixels_u8 ERROR_PIXELS = {0, 0, 0, NULL};
	microjxl__inner *inner;
	microjxl__plane *p;
	microjxl_pixels_u8 pixels;

	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC) return ERROR_PIXELS;
	inner = frame->inner;
	if (!inner || inner->magic != MICROJXL__INNER_MAGIC) return ERROR_PIXELS;
	p = microjxl__frame_rendered_plane_planar(frame, channel, MICROJXL__PLANE_U8,
		microjxl__planar_cache_slot(inner, MICROJXL__PLANE_U8, channel));
	if (!p) return ERROR_PIXELS;
	pixels.width = p->width;
	pixels.height = p->height;
	pixels.stride_bytes = p->stride_bytes;
	pixels.data = (void*) p->pixels;
	return pixels;
}

MICROJXL_API const uint8_t *microjxl_row_u8(microjxl_pixels_u8 pixels, int32_t y) {
	MICROJXL__ASSERT(0 <= y && y < pixels.height);
	MICROJXL__ASSERT(pixels.stride_bytes > 0);
	MICROJXL__ASSERT(pixels.data);
	return (const uint8_t*) (uintptr_t) ((const char*) pixels.data + (size_t) pixels.stride_bytes * (size_t) y);
}

MICROJXL_API microjxl_pixels_u16 microjxl_frame_pixels_u16(const microjxl_frame *frame, int32_t channel) {
	static const microjxl_pixels_u16 ERROR_PIXELS = {0, 0, 0, NULL};
	microjxl__inner *inner;
	microjxl__plane *p;
	microjxl_pixels_u16 pixels;

	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC) return ERROR_PIXELS;
	inner = frame->inner;
	if (!inner || inner->magic != MICROJXL__INNER_MAGIC) return ERROR_PIXELS;
	p = microjxl__frame_rendered_plane_planar(frame, channel, MICROJXL__PLANE_U16,
		microjxl__planar_cache_slot(inner, MICROJXL__PLANE_U16, channel));
	if (!p) return ERROR_PIXELS;
	pixels.width = p->width;
	pixels.height = p->height;
	pixels.stride_bytes = p->stride_bytes;
	pixels.data = (void*) p->pixels;
	return pixels;
}

MICROJXL_API const uint16_t *microjxl_row_u16(microjxl_pixels_u16 pixels, int32_t y) {
	MICROJXL__ASSERT(0 <= y && y < pixels.height);
	MICROJXL__ASSERT(pixels.stride_bytes > 0);
	MICROJXL__ASSERT(pixels.data);
	return (const uint16_t*) (uintptr_t) ((const char*) pixels.data + (size_t) pixels.stride_bytes * (size_t) y);
}

MICROJXL_API microjxl_pixels_f32 microjxl_frame_pixels_f32(const microjxl_frame *frame, int32_t channel) {
	static const microjxl_pixels_f32 ERROR_PIXELS = {0, 0, 0, NULL};
	microjxl__inner *inner;
	microjxl__plane *p;
	microjxl_pixels_f32 pixels;

	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC) return ERROR_PIXELS;
	inner = frame->inner;
	if (!inner || inner->magic != MICROJXL__INNER_MAGIC) return ERROR_PIXELS;
	p = microjxl__frame_rendered_plane_planar(frame, channel, MICROJXL__PLANE_F32,
		microjxl__planar_cache_slot(inner, MICROJXL__PLANE_F32, channel));
	if (!p) return ERROR_PIXELS;
	pixels.width = p->width;
	pixels.height = p->height;
	pixels.stride_bytes = p->stride_bytes;
	pixels.data = (void*) p->pixels;
	return pixels;
}

MICROJXL_API const float *microjxl_row_f32(microjxl_pixels_f32 pixels, int32_t y) {
	MICROJXL__ASSERT(0 <= y && y < pixels.height);
	MICROJXL__ASSERT(pixels.stride_bytes > 0);
	MICROJXL__ASSERT(pixels.data);
	return (const float*) (uintptr_t) ((const char*) pixels.data + (size_t) pixels.stride_bytes * (size_t) y);
}

MICROJXL_API void microjxl_free(microjxl_image *image) {
	microjxl__inner *inner;
	microjxl__check_image(image, MICROJXL__ORIGIN_free, &inner);
	if (inner) microjxl__mem_free_inner(inner);
	image->magic = MICROJXL__IMAGE_ERR_MAGIC ^ MICROJXL__ORIGIN_NEXT;
	image->u.err = MICROJXL__4("Ufre");
}

// frame metadata accessors: see the declarations for semantics. All return
// safe defaults for invalid/missing frames.
MICROJXL_API int32_t microjxl_frame_x0(const microjxl_frame *frame) {
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC || !frame->inner) return 0;
	return frame->inner->frame_x0;
}
MICROJXL_API int32_t microjxl_frame_y0(const microjxl_frame *frame) {
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC || !frame->inner) return 0;
	return frame->inner->frame_y0;
}
MICROJXL_API int32_t microjxl_frame_width(const microjxl_frame *frame) {
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC || !frame->inner) return 0;
	return frame->inner->frame_width;
}
MICROJXL_API int32_t microjxl_frame_height(const microjxl_frame *frame) {
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC || !frame->inner) return 0;
	return frame->inner->frame_height;
}
MICROJXL_API int64_t microjxl_frame_duration(const microjxl_frame *frame) {
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC || !frame->inner) return 0;
	return frame->inner->frame_duration;
}
MICROJXL_API int microjxl_frame_is_last(const microjxl_frame *frame) {
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC || !frame->inner) return 0;
	return frame->inner->frame_is_last;
}

MICROJXL_API int32_t microjxl_num_extra_channels(const microjxl_image *image) {
	microjxl__inner *inner; // ignored
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.num_extra_channels;
}

MICROJXL_API int32_t microjxl_extra_channel_type(const microjxl_image *image, int32_t ec) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return -1;
	if (ec < 0 || ec >= image->u.inner->image.num_extra_channels) return -1;
	return (int32_t) image->u.inner->image.ec_info[ec].type;
}

MICROJXL_API int32_t microjxl_extra_channel_bpp(const microjxl_image *image, int32_t ec) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	if (ec < 0 || ec >= image->u.inner->image.num_extra_channels) return 0;
	return image->u.inner->image.ec_info[ec].bpp;
}

MICROJXL_API int32_t microjxl_extra_channel_exp_bits(const microjxl_image *image, int32_t ec) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	if (ec < 0 || ec >= image->u.inner->image.num_extra_channels) return 0;
	return image->u.inner->image.ec_info[ec].exp_bits;
}

MICROJXL_API microjxl_pixels_f32 microjxl_frame_extra_channel_f32(const microjxl_frame *frame, int32_t ec) {
	static const microjxl_pixels_f32 ERROR_PIXELS = {0, 0, 0, NULL};
	microjxl__st stbuf;
	microjxl__inner *inner;
	microjxl__image_st *im;
	microjxl__plane *src = NULL;
	microjxl_pixels_f32 pixels;
	int32_t cc, n_fe, y, x;

	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC) return ERROR_PIXELS;
	inner = frame->inner;
	if (!inner || inner->magic != MICROJXL__INNER_MAGIC) return ERROR_PIXELS;
	im = &inner->image;
	if (!inner->rendered) return ERROR_PIXELS;
	if (ec < 0 || ec >= im->num_extra_channels) return ERROR_PIXELS;

	/* source plane: in coalesced mode the canvas-composited EC plane; in
	 * non-coalesced mode the frame's own EC plane (when the frame carries
	 * it) */
	cc = (inner->frame.gmodular.num_channels >= 3) ? 3 : 1;
	n_fe = inner->frame.gmodular.num_channels > cc
		? inner->frame.gmodular.num_channels - cc : 0;
	if (inner->coalescing) {
		if (im->ec_canvas && ec < im->num_ec_canvas &&
		    im->ec_canvas[ec].type != MICROJXL__PLANE_EMPTY)
			src = &im->ec_canvas[ec];
	} else if (ec < n_fe && inner->frame.gmodular.channel[cc + ec].type != MICROJXL__PLANE_EMPTY) {
		src = &inner->frame.gmodular.channel[cc + ec];
	}
	if (!src) return ERROR_PIXELS; // channel not present in this stream

	/* convert raw samples (I16/I32) to float 0..1 via the EC's own scale */
	if (inner->rendered_ec_idx != ec ||
	    inner->rendered_ec.type != MICROJXL__PLANE_F32 ||
	    inner->rendered_ec.width != src->width || inner->rendered_ec.height != src->height) {
		microjxl__init_state(&stbuf, inner);
		microjxl__mem_free_plane(&inner->rendered_ec);
		microjxl__init_empty_plane(&inner->rendered_ec);
		if (microjxl__init_plane(&stbuf, MICROJXL__PLANE_F32, src->width, src->height, MICROJXL__PLANE_FORCE_PAD, &inner->rendered_ec)) {
			inner->origin = MICROJXL__ORIGIN_frame_pixels;
			inner->err = stbuf.err;
			microjxl__init_empty_plane(&inner->rendered_ec);
			return ERROR_PIXELS;
		}
	}
	{
		if (src->type == MICROJXL__PLANE_F32) {
			/* coalesced EC canvas: already normalized float */
			for (y = 0; y < src->height; ++y) {
				const float *s = MICROJXL__F32_PIXELS(src, y);
				float *d = MICROJXL__F32_PIXELS(&inner->rendered_ec, y);
				memcpy(d, s, (size_t) src->width * sizeof(float));
			}
		} else {
			const microjxl__ec_info *ecp = &im->ec_info[ec];
			float scale = (float) microjxl__maxpixel_alpha(ecp);
			for (y = 0; y < src->height; ++y) {
				if (src->type == MICROJXL__PLANE_I32) {
					const int32_t *s = MICROJXL__I32_PIXELS(src, y);
					float *d = MICROJXL__F32_PIXELS(&inner->rendered_ec, y);
					for (x = 0; x < src->width; ++x) d[x] = (float) s[x] / scale;
				} else {
					const int16_t *s = MICROJXL__I16_PIXELS(src, y);
					float *d = MICROJXL__F32_PIXELS(&inner->rendered_ec, y);
					for (x = 0; x < src->width; ++x) d[x] = (float) s[x] / scale;
				}
			}
		}
	}
	inner->rendered_ec_idx = ec;
	pixels.width = inner->rendered_ec.width;
	pixels.height = inner->rendered_ec.height;
	pixels.stride_bytes = inner->rendered_ec.stride_bytes;
	pixels.data = (void*) inner->rendered_ec.pixels;
	return pixels;
}

MICROJXL_API int32_t microjxl_animation_tps_num(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.anim_tps_num;
}

MICROJXL_API int32_t microjxl_animation_tps_denom(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.anim_tps_denom;
}

MICROJXL_API int64_t microjxl_animation_nloops(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.anim_nloops;
}

MICROJXL_API float microjxl_intensity_target(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 255.0f;
	return image->u.inner->image.intensity_target;
}

MICROJXL_API float microjxl_min_nits(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0.0f;
	return image->u.inner->image.min_nits;
}

MICROJXL_API float microjxl_linear_below(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0.0f;
	return image->u.inner->image.linear_below;
}

MICROJXL_API float microjxl_relative_to_max_display(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0.0f;
	return image->u.inner->image.relative_to_max_display;
}

MICROJXL_API int32_t microjxl_bits_per_sample(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.bpp;
}

MICROJXL_API int32_t microjxl_exponent_bits_per_sample(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.exp_bits;
}

MICROJXL_API int32_t microjxl_xsize(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.width;
}

MICROJXL_API int32_t microjxl_ysize(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.height;
}

MICROJXL_API int32_t microjxl_orientation(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 1;
	return (int32_t) image->u.inner->image.orientation;
}

MICROJXL_API int32_t microjxl_is_grayscale(const microjxl_image *image) {
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return 0;
	return image->u.inner->image.cspace == MICROJXL__CS_GREY ? 1 : 0;
}

MICROJXL_API const char *microjxl_frame_name(const microjxl_frame *frame) {
	if (!frame || frame->magic != MICROJXL__FRAME_MAGIC || !frame->inner) return "";
	if (frame->inner->frame.name_len <= 0 || !frame->inner->frame.name) return "";
	/* the decoded name is not guaranteed NUL-terminated within itself;
	 * names are short (<= name_len bytes) and frame.name is a char* owned
	 * by the frame state, which was allocated name_len+1 by microjxl__name */
	return frame->inner->frame.name;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
// ICC profile generation for streams that use the enum ColourEncoding
// (want_icc == 0). Port of libjxl's MaybeCreateProfile
// (lib/jxl/cms/jxl_cms_internal.h) so that decoders can hand out a usable
// ICC profile for every image. The generated profile follows the exact tag
// layout of libjxl (desc/cprt/wtpt[/chad]/[cicp]/rXYZ gXYZ bXYZ/TRC or the
// XYB A2B0/B2A0 mAB tags); small float-rounding differences are possible.

#ifdef MICROJXL_IMPLEMENTATION

typedef struct { uint8_t *data; size_t size, cap; int err; } microjxl__geniccbuf;

static int microjxl__genicc_put(microjxl__geniccbuf *b, size_t pos, const void *src, size_t n) {
	if (b->err) return 0;
	if (pos + n > b->size) {
		size_t newcap = b->cap ? b->cap : 256;
		while (newcap < pos + n) newcap *= 2;
		uint8_t *np = (uint8_t *) MICROJXL_REALLOC(b->data, newcap);
		if (!np) { b->err = 1; return 0; }
		if (b->size < newcap) memset(np + b->size, 0, newcap - b->size);
		b->data = np;
		b->cap = newcap;
	}
	if (src) memcpy(b->data + pos, src, n);
	else memset(b->data + pos, 0, n);
	if (pos + n > b->size) b->size = pos + n;
	return 1;
}
static int microjxl__genicc_u32(microjxl__geniccbuf *b, size_t pos, uint32_t v) {
	uint8_t t[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
	return microjxl__genicc_put(b, pos, t, 4);
}
static int microjxl__genicc_u16(microjxl__geniccbuf *b, size_t pos, uint32_t v) {
	uint8_t t[2] = { (uint8_t)(v >> 8), (uint8_t)v };
	return microjxl__genicc_put(b, pos, t, 2);
}
static int microjxl__genicc_u8(microjxl__geniccbuf *b, size_t pos, uint8_t v) {
	return microjxl__genicc_put(b, pos, &v, 1);
}
static int microjxl__genicc_tag(microjxl__geniccbuf *b, size_t pos, const char tag[4]) {
	return microjxl__genicc_put(b, pos, tag, 4);
}
static void microjxl__genicc_u32buf(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t) v;
}
static int microjxl__genicc_s15f16(microjxl__geniccbuf *b, size_t pos, float value) {
	if (!(value >= -32767.995f && value <= 32767.995f)) { b->err = 1; return 0; }
	int32_t i = (int32_t) lroundf(value * 65536.0f);
	return microjxl__genicc_u32(b, pos, (uint32_t) i);
}

/* TF helpers on |x| (transfer_functions.h), display<->encoded for PQ/HLG. */
static double microjxl__icc_pq_display(double e, double it) {
	static const double kM1 = 2610.0 / 16384, kM2 = (2523.0 / 4096) * 128;
	static const double kC1 = 3424.0 / 4096, kC2 = (2413.0 / 4096) * 32, kC3 = (2392.0 / 4096) * 32;
	if (e == 0.0) return 0.0;
	e = fabs(e);
	double xp = pow(e, 1.0 / kM2);
	double num = xp - kC1; if (num < 0) num = 0;
	double den = kC2 - kC3 * xp;
	double d = pow(num / den, 1.0 / kM1);
	return d * (10000.0 / it);
}
static double microjxl__icc_pq_encode(double d, double it) {
	static const double kM1 = 2610.0 / 16384, kM2 = (2523.0 / 4096) * 128;
	static const double kC1 = 3424.0 / 4096, kC2 = (2413.0 / 4096) * 32, kC3 = (2392.0 / 4096) * 32;
	if (d == 0.0) return 0.0;
	d = fabs(d);
	double xp = pow(d * (it * (1.0 / 10000.0)), kM1);
	double e = pow((kC1 + xp * kC2) / (1.0 + xp * kC3), kM2);
	return e;
}
static double microjxl__icc_hlg_display(double e) { /* InvOETF; OOTF is identity */
	const double kA = 0.17883277, kRA = 1.0 / kA, kB = 1 - 4 * kA, kC = 0.5599107295, kInv12 = 1.0 / 12.0;
	if (e == 0.0) return 0.0;
	e = fabs(e);
	if (e <= 0.5) return e * e * (1.0 / 3);
	return (exp((e - kC) * kRA) + kB) * kInv12;
}

/* Rec2408ToneMapperBase({0,10000},{0,250},lum) for PQ. */
static void microjxl__icc_rec2408_tonemap(float rgb[3], const float lum[3]) {
	const float src_peak = 10000.0f, tgt_peak = 250.0f;
	float pq_min = (float) microjxl__icc_pq_encode(0.0, 1.0); /* = 0 */
	float pq_max = (float) microjxl__icc_pq_encode(1.0, 1.0);
	float pq_range = pq_max - pq_min, inv_range = 1.0f / pq_range;
	float min_lum = ((float) microjxl__icc_pq_encode(0.0, 1.0) - pq_min) * inv_range;
	float max_lum = ((float) microjxl__icc_pq_encode(tgt_peak / 10000.0f, 1.0) - pq_min) * inv_range;
	float ks = 1.5f * max_lum - 0.5f;
	float inv_one_minus_ks = 1.0f / (1.0f - ks > 1e-6f ? 1.0f - ks : 1e-6f);
	float normalizer = src_peak / tgt_peak, inv_target_peak = 1.0f / tgt_peak;
	float luminance = src_peak * (lum[0] * rgb[0] + lum[1] * rgb[1] + lum[2] * rgb[2]);
	float normalized_pq = ((float) microjxl__icc_pq_encode(luminance, 1.0) - pq_min) * inv_range;
	if (normalized_pq > 1.0f) normalized_pq = 1.0f;
	float e2, t, t2, t3;
	if (normalized_pq < ks) e2 = normalized_pq;
	else { t = (normalized_pq - ks) * inv_one_minus_ks; t2 = t * t; t3 = t2 * t;
		e2 = (2 * t3 - 3 * t2 + 1) * ks + (t3 - 2 * t2 + t) * (1 - ks) + (-2 * t3 + 3 * t2) * max_lum; }
	float one_minus_e2 = 1 - e2, one_minus_e2_2 = one_minus_e2 * one_minus_e2;
	float one_minus_e2_4 = one_minus_e2_2 * one_minus_e2_2;
	float e3 = min_lum * one_minus_e2_4 + e2;
	float e4 = e3 * pq_range + pq_min;
	float d4 = (float) microjxl__icc_pq_display(e4, 1.0);
	float new_luminance = d4 < 0.0f ? 0.0f : d4 > tgt_peak ? tgt_peak : d4;
	const float min_luminance = 1e-6f;
	int use_cap = luminance <= min_luminance;
	float ratio = new_luminance / (luminance > min_luminance ? luminance : min_luminance);
	float cap = new_luminance * inv_target_peak;
	float multiplier = ratio * normalizer;
	for (int i = 0; i < 3; ++i) rgb[i] = use_cap ? cap : rgb[i] * multiplier;
}

/* HlgOOTF_Base(300, 80, lum) */
static void microjxl__icc_hlg_ootf(float rgb[3], const float lum[3]) {
	float gamma = (float) pow(1.111, log2(80.0 / 300.0));
	float exponent = gamma - 1.0f;
	if (!(exponent < -0.01f || 0.01f < exponent)) return;
	float luminance = lum[0] * rgb[0] + lum[1] * rgb[1] + lum[2] * rgb[2];
	float ratio = (float) pow(luminance, exponent);
	if (ratio > 1e9f) ratio = 1e9f;
	rgb[0] *= ratio; rgb[1] *= ratio; rgb[2] *= ratio;
}

/* GamutMapScalar(rgb, lum, preserve_saturation=0.3) */
static void microjxl__icc_gamut_map(float rgb[3], const float lum[3]) {
	float luminance = lum[0] * rgb[0] + lum[1] * rgb[1] + lum[2] * rgb[2];
	float gray_mix_saturation = 0.0f, gray_mix_luminance = 0.0f;
	for (int i = 0; i < 3; ++i) {
		float val = rgb[i];
		float val_minus_gray = val - luminance;
		float inv_val_minus_gray = 1.0f / (val_minus_gray == 0.0f ? 1.0f : val_minus_gray);
		float val_over_val_minus_gray = val * inv_val_minus_gray;
		gray_mix_saturation = (val_minus_gray >= 0.0f) ? gray_mix_saturation
			: (val_over_val_minus_gray > gray_mix_saturation ? val_over_val_minus_gray : gray_mix_saturation);
		gray_mix_luminance = (val_minus_gray <= 0.0f) ? (gray_mix_saturation > gray_mix_luminance ? gray_mix_saturation : gray_mix_luminance)
			: (val_over_val_minus_gray - inv_val_minus_gray > gray_mix_luminance ? val_over_val_minus_gray - inv_val_minus_gray : gray_mix_luminance);
	}
	float gm = 0.3f * (gray_mix_saturation - gray_mix_luminance) + gray_mix_luminance;
	gm = gm < 0.0f ? 0.0f : gm > 1.0f ? 1.0f : gm;
	for (int i = 0; i < 3; ++i) rgb[i] = gm * (luminance - rgb[i]) + rgb[i];
	float max_clr = 1.0f;
	for (int i = 0; i < 3; ++i) if (rgb[i] > max_clr) max_clr = rgb[i];
	for (int i = 0; i < 3; ++i) rgb[i] *= 1.0f / max_clr;
}

/* ToneMapPixel: tone-mapped CIELAB (8-bit PCS Lab) for the HDR mft1 CLUT. */
static int microjxl__icc_tonemap_pixel(
	const float cp[4][2] /*white,red,green,blue xy*/, int is_pq,
	const float in[3], uint8_t out[3]
) {
	float toxyz[3][3], chad[3][3], to_xyzd50[3][3];
	if (!microjxl__primaries_to_xyz(cp, toxyz)) return 0;
	float lum[3] = { toxyz[1][0], toxyz[1][1], toxyz[1][2] };
	float linear[3];
	for (int i = 0; i < 3; ++i)
		linear[i] = is_pq ? (float) microjxl__icc_pq_display(in[i], 10000.0) : (float) microjxl__icc_hlg_display(in[i]);
	if (is_pq) microjxl__icc_rec2408_tonemap(linear, lum);
	else microjxl__icc_hlg_ootf(linear, lum);
	microjxl__icc_gamut_map(linear, lum);
	if (!microjxl__adapt_to_xyzd50(cp[0][0], cp[0][1], chad)) return 0;
	microjxl__mul3x3(chad, toxyz, to_xyzd50);
	float xyz[3] = {0, 0, 0};
	for (int xc = 0; xc < 3; ++xc)
		for (int rc = 0; rc < 3; ++rc)
			xyz[xc] += linear[rc] * to_xyzd50[xc][rc];
	const float kDelta = 6.0f / 29.0f, kXn = 0.964212f, kZn = 0.825188f;
	float fx = xyz[0] / kXn, fy = xyz[1], fz = xyz[2] / kZn;
	fx = fx <= kDelta * kDelta * kDelta ? fx * (1.0f / (3 * kDelta * kDelta)) + 4.0f / 29.0f : cbrtf(fx);
	fy = fy <= kDelta * kDelta * kDelta ? fy * (1.0f / (3 * kDelta * kDelta)) + 4.0f / 29.0f : cbrtf(fy);
	fz = fz <= kDelta * kDelta * kDelta ? fz * (1.0f / (3 * kDelta * kDelta)) + 4.0f / 29.0f : cbrtf(fz);
	float L = 1.16f * fy - 0.16f, A = 500 * (fx - fy), B = 200 * (fy - fz);
	out[0] = (uint8_t) lroundf(255.0f * (L < 0.0f ? 0.0f : L > 1.0f ? 1.0f : L));
	out[1] = (uint8_t) lroundf(128.0f + (A < -128.0f ? -128.0f : A > 127.0f ? 127.0f : A));
	out[2] = (uint8_t) lroundf(128.0f + (B < -128.0f ? -128.0f : B > 127.0f ? 127.0f : B));
	return 1;
}

/* CreateICCCurvParaTag / curv / mluc / XYZ / sf32 tag emitters. */
static int microjxl__genicc_para(microjxl__geniccbuf *b, const float *params, int nparams, int curve_type) {
	size_t pos = b->size;
	if (!microjxl__genicc_tag(b, pos, "para") || !microjxl__genicc_u32(b, pos + 4, 0) ||
		!microjxl__genicc_u16(b, pos + 8, (uint32_t) curve_type) || !microjxl__genicc_u16(b, pos + 10, 0)) return 0;
	for (int i = 0; i < nparams; ++i)
		if (!microjxl__genicc_s15f16(b, b->size, params[i])) return 0;
	return 1;
}
static int microjxl__genicc_mluc(microjxl__geniccbuf *b, const char *text, size_t textlen) {
	size_t pos = b->size;
	if (!microjxl__genicc_tag(b, pos, "mluc") || !microjxl__genicc_u32(b, pos + 4, 0) ||
		!microjxl__genicc_u32(b, pos + 8, 1) || !microjxl__genicc_u32(b, pos + 12, 12) ||
		!microjxl__genicc_tag(b, pos + 16, "enUS") || !microjxl__genicc_u32(b, pos + 20, (uint32_t)(textlen * 2)) ||
		!microjxl__genicc_u32(b, pos + 24, 28)) return 0;
	for (size_t i = 0; i < textlen; ++i) {
		if (!microjxl__genicc_u8(b, b->size, 0) || !microjxl__genicc_u8(b, b->size, (uint8_t) text[i])) return 0;
	}
	return 1;
}
static int microjxl__genicc_xyz3(microjxl__geniccbuf *b, const float v[3]) {
	size_t pos = b->size;
	if (!microjxl__genicc_tag(b, pos, "XYZ ") || !microjxl__genicc_u32(b, pos + 4, 0)) return 0;
	for (int i = 0; i < 3; ++i) if (!microjxl__genicc_s15f16(b, b->size, v[i])) return 0;
	return 1;
}
static int microjxl__genicc_sf32m(microjxl__geniccbuf *b, const float m[3][3]) {
	size_t pos = b->size;
	if (!microjxl__genicc_tag(b, pos, "sf32") || !microjxl__genicc_u32(b, pos + 4, 0)) return 0;
	for (int j = 0; j < 3; ++j) for (int i = 0; i < 3; ++i)
		if (!microjxl__genicc_s15f16(b, b->size, m[j][i])) return 0;
	return 1;
}
static int microjxl__genicc_table64(microjxl__geniccbuf *b, int is_pq) {
	/* CreateTableCurve<64, kPQ/kHLG>(tone_map=true) */
	uint16_t table[64];
	for (int i = 0; i < 64; ++i) {
		double x = (double) i / 63.0, y;
		if (is_pq) {
			y = microjxl__icc_pq_display(x, 10000.0);
			float g[3] = { (float) y * 10000.0f / 10000.0f, (float) y, (float) y };
			/* gray pixel: tone-map with equal channels, luminance rows 1/3 */
			float lum[3] = { 1.0f / 3, 1.0f / 3, 1.0f / 3 };
			float tone[3] = { g[0], g[0], g[0] };
			microjxl__icc_rec2408_tonemap(tone, lum);
			y = tone[0];
		} else {
			y = microjxl__icc_hlg_display(x);
		}
		if (y < 0.0) y = 0.0; if (y > 1.0) y = 1.0;
		table[i] = (uint16_t) lround(y * 65535.0);
	}
	size_t pos = b->size;
	if (!microjxl__genicc_tag(b, pos, "curv") || !microjxl__genicc_u32(b, pos + 4, 0) ||
		!microjxl__genicc_u32(b, pos + 8, 64)) return 0;
	for (int i = 0; i < 64; ++i) if (!microjxl__genicc_u16(b, b->size, table[i])) return 0;
	return 1;
}

/* CreateICCLutAtoBTagForXYB. */
static int microjxl__genicc_xyb_a2b(microjxl__geniccbuf *b) {
	/* XYB constants (opsin_params.h), computed in float like libjxl constexprs */
	const float kNegBias[3] = { -0.0037930732552754493f, -0.0037930732552754493f, -0.0037930732552754493f };
	const float off0 = 0.015386134f, off1 = 0.0f - 0.015386134f + 1.0f / 22.995788804f, off2 = 0.0f + 0.27770459f;
	const float sc0 = (22.995788804f * 1.183000077f) / (22.995788804f + 1.183000077f);
	const float sc1 = sc0;
	const float sc2 = (1.183000077f * 1.502141333f) / (1.183000077f + 1.502141333f);
	const float off[3] = { off0, off1, off2 }, sc[3] = { sc0, sc1, sc2 };
	size_t pos = b->size;
	if (!microjxl__genicc_tag(b, pos, "mAB ") || !microjxl__genicc_u32(b, pos + 4, 0) ||
		!microjxl__genicc_u8(b, pos + 8, 3) || !microjxl__genicc_u8(b, pos + 9, 3) ||
		!microjxl__genicc_u16(b, pos + 10, 0) ||
		!microjxl__genicc_u32(b, pos + 12, 32) || !microjxl__genicc_u32(b, pos + 16, 244) ||
		!microjxl__genicc_u32(b, pos + 20, 148) || !microjxl__genicc_u32(b, pos + 24, 80) ||
		!microjxl__genicc_u32(b, pos + 28, 32)) return 0;
	float one = 1.0f;
	for (int i = 0; i < 3; ++i) if (!microjxl__genicc_para(b, &one, 1, 0)) return 0;
	for (int i = 0; i < 16; ++i) if (!microjxl__genicc_u8(b, b->size, (uint8_t)(i < 3 ? 2 : 0))) return 0;
	if (!microjxl__genicc_u8(b, b->size, 2) || !microjxl__genicc_u8(b, b->size, 0) ||
		!microjxl__genicc_u16(b, b->size, 0)) return 0;
	for (int ix = 0; ix < 2; ++ix) for (int iy = 0; iy < 2; ++iy) for (int ib = 0; ib < 2; ++ib) {
		float xyb[3], s[3];
		xyb[0] = ix / sc[0] - off[0]; xyb[1] = iy / sc[1] - off[1]; xyb[2] = ib / sc[2] - off[2];
		s[0] = (xyb[1] + xyb[0] + off[0]) * sc[0];
		s[1] = (xyb[1] - xyb[0] + off[1]) * sc[1];
		s[2] = (xyb[2] + xyb[1] + off[2]) * sc[2];
		for (int i = 0; i < 3; ++i) {
			int32_t val = (int32_t) lroundf(65535.0f * s[i]);
			if (val < 0) val = 0; if (val > 65535) val = 65535;
			if (!microjxl__genicc_u16(b, b->size, (uint32_t) val)) return 0;
		}
	}
	for (int i = 0; i < 3; ++i) {
		float bb = -off[i] - cbrtf(kNegBias[i]);
		float params[5] = { 3.0f, 1.0f / sc[i], bb, 0.0f, (-bb * sc[i]) > 0.0f ? -bb * sc[i] : 0.0f };
		if (!microjxl__genicc_para(b, params, 5, 3)) return 0;
	}
	const float matrix[9] = { 1.5170095f, -1.1065225f, 0.071623f,
		 -0.050022f, 0.5683655f, -0.018344f,
		 -1.387676f, 1.1145555f, 0.6857255f };
	for (int i = 0; i < 9; ++i) if (!microjxl__genicc_s15f16(b, b->size, matrix[i])) return 0;
	for (int i = 0; i < 3; ++i) {
		float intercept = 0;
		for (int j = 0; j < 3; ++j) intercept += matrix[i * 3 + j] * kNegBias[j];
		if (!microjxl__genicc_s15f16(b, b->size, intercept)) return 0;
	}
	return 1;
}

/* CreateICCNoOpBToATag. */
static int microjxl__genicc_b2a(microjxl__geniccbuf *b) {
	size_t pos = b->size;
	if (!microjxl__genicc_tag(b, pos, "mBA ") || !microjxl__genicc_u32(b, pos + 4, 0) ||
		!microjxl__genicc_u8(b, pos + 8, 3) || !microjxl__genicc_u8(b, pos + 9, 3) ||
		!microjxl__genicc_u16(b, pos + 10, 0) ||
		!microjxl__genicc_u32(b, pos + 12, 32) || !microjxl__genicc_u32(b, pos + 16, 0) ||
		!microjxl__genicc_u32(b, pos + 20, 0) || !microjxl__genicc_u32(b, pos + 24, 0) ||
		!microjxl__genicc_u32(b, pos + 28, 0)) return 0;
	float one = 1.0f;
	for (int i = 0; i < 3; ++i) if (!microjxl__genicc_para(b, &one, 1, 0)) return 0;
	return 1;
}

/* MD5 (RFC 1321) over the profile with fields 44..48/64..68 zeroed —
 * libjxl ICCComputeMD5. */
static void microjxl__genicc_md5(const uint8_t *data, size_t size, uint8_t sum[16]) {
	static const uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
	};
	static const uint32_t S[64] = {
		7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22, 5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
		4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23, 6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21,
	};
	size_t padded = size + 1 + ((64 - ((size + 1 + 8) & 63)) & 63) + 8;
	uint8_t *msg = (uint8_t *) MICROJXL_MALLOC(padded);
	if (!msg) return;
	memcpy(msg, data, size);
	msg[size] = 0x80;
	memset(msg + size + 1, 0, padded - size - 1 - 8);
	uint64_t bits = (uint64_t) size * 8;
	for (int i = 0; i < 8; ++i) msg[padded - 8 + i] = (uint8_t)(bits >> (8 * i));
	uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
	for (size_t off = 0; off < padded; off += 64) {
		uint32_t M[16], a = a0, b = b0, c = c0, d = d0;
		for (int i = 0; i < 16; ++i)
			M[i] = (uint32_t) msg[off + 4 * i] | ((uint32_t) msg[off + 4 * i + 1] << 8) |
				((uint32_t) msg[off + 4 * i + 2] << 16) | ((uint32_t) msg[off + 4 * i + 3] << 24);
		for (int i = 0; i < 64; ++i) {
			uint32_t f, g;
			if (i < 16) { f = (b & c) | (~b & d); g = i; }
			else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
			else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
			else { f = c ^ (b | ~d); g = (7 * i) & 15; }
			uint32_t tmp = d; d = c; c = b;
			b = b + ((a + f + K[i] + M[g]) << S[i] | (a + f + K[i] + M[g]) >> (32 - S[i]));
			a = tmp;
		}
		a0 += a; b0 += b; c0 += c; d0 += d;
	}
	microjxl__mem_free(msg);
	for (int i = 0; i < 4; ++i) {
		sum[i] = (uint8_t)(a0 >> (8 * i)); sum[4 + i] = (uint8_t)(b0 >> (8 * i));
		sum[8 + i] = (uint8_t)(c0 >> (8 * i)); sum[12 + i] = (uint8_t)(d0 >> (8 * i));
	}
}

/* Builds the description string (ColorEncodingDescriptionImpl). Returns buf. */
static const char *microjxl__genicc_desc(const microjxl__image_st *im, char *buf, size_t buflen) {
	static const float D65[2] = {0.3127f, 0.3290f}, E[2] = {1.0f/3.0f, 1.0f/3.0f}, DCI_WP[2] = {0.314f, 0.351f};
	static const float SRGP[3][2] = {{0.639998686f,0.330010138f},{0.300003784f,0.600003357f},{0.150002046f,0.059997204f}};
	static const float P3P[3][2] = {{0.680f,0.320f},{0.265f,0.690f},{0.150f,0.060f}};
	static const float BT2100P[3][2] = {{0.708f,0.292f},{0.170f,0.797f},{0.131f,0.046f}};
	int cs = im->cspace;
	const char *css = cs == MICROJXL__CS_CHROMA ? "RGB" : cs == MICROJXL__CS_GREY ? "Gra" : "XYB";
	const char *wp = NULL, *pr = NULL, *tf = NULL, *ri;
	char num[128] = "";
	float wpx = im->cpoints[0][0], wpy = im->cpoints[0][1];
	if (fabsf(wpx - D65[0]) < 3e-5f && fabsf(wpy - D65[1]) < 3e-5f) wp = "D65";
	else if (fabsf(wpx - DCI_WP[0]) < 3e-5f && fabsf(wpy - DCI_WP[1]) < 3e-5f) wp = "DCI";
	else if (fabsf(wpx - E[0]) < 3e-5f && fabsf(wpy - E[1]) < 3e-5f) wp = "EER";
	if (im->gamma_or_tf == -13) tf = "SRG";
	else if (im->gamma_or_tf == -8) tf = "Lin";
	else if (im->gamma_or_tf == -1) tf = "709";
	else if (im->gamma_or_tf == -16) tf = "PeQ";
	else if (im->gamma_or_tf == -18) tf = "HLG";
	else if (im->gamma_or_tf == -17) tf = "DCI";
	else if (im->gamma_or_tf == -2) tf = "TF?";
	/* libjxl ColorEncodingDescriptionImpl: the TF suffix is emitted for every
	 * colour space except XYB, and for a gamma TF it is "g" + ToString(gamma)
	 * (base/common.h ToString uses "%g"). The old `!= CHROMA` guard skipped the
	 * gamma suffix for RGB files (e.g. RGB_D65_SRG_Rel_g0.45455 lost its tail). */
	if (cs != MICROJXL__CS_XYB && im->gamma_or_tf > 0) {
		snprintf(num, sizeof num, "g%g", (double) im->gamma_or_tf * 1e-7);
	}
	if (cs == MICROJXL__CS_CHROMA) {
		float rpx = im->cpoints[1][0], rpy = im->cpoints[1][1];
		float gpx = im->cpoints[2][0], gpy = im->cpoints[2][1];
		float bpx = im->cpoints[3][0], bpy = im->cpoints[3][1];
		int srgb = 1, p3 = 1, bt = 1;
		const float *tests[3][3] = { {SRGP[0],SRGP[1],SRGP[2]}, {P3P[0],P3P[1],P3P[2]}, {BT2100P[0],BT2100P[1],BT2100P[2]} };
		float cps[3][2] = {{rpx,rpy},{gpx,gpy},{bpx,bpy}};
		for (int i = 0; i < 3; ++i) {
			if (!(fabsf(cps[i][0]-SRGP[i][0]) < 3e-5f && fabsf(cps[i][1]-SRGP[i][1]) < 3e-5f)) srgb = 0;
			if (!(fabsf(cps[i][0]-P3P[i][0]) < 3e-5f && fabsf(cps[i][1]-P3P[i][1]) < 3e-5f)) p3 = 0;
			if (!(fabsf(cps[i][0]-BT2100P[i][0]) < 3e-5f && fabsf(cps[i][1]-BT2100P[i][1]) < 3e-5f)) bt = 0;
			(void) tests;
		}
		if (srgb) pr = "SRG"; else if (bt) pr = "202"; else if (p3) pr = "DCI";
	}
	ri = im->render_intent == MICROJXL__INTENT_PERC ? "Per" : im->render_intent == MICROJXL__INTENT_REL ? "Rel"
		: im->render_intent == MICROJXL__INTENT_SAT ? "Sat" : "Abs";
	/* Assemble: cs [_wp] [_pr] _ri [_tf] */
	snprintf(buf, buflen, "%s", css);
	if (cs != MICROJXL__CS_XYB) {
		strncat(buf, "_", buflen - strlen(buf) - 1);
		if (wp) strncat(buf, wp, buflen - strlen(buf) - 1);
		else {
			char tail[64];
			snprintf(tail, sizeof tail, "%g;%g", wpx, wpy);
			strncat(buf, tail, buflen - strlen(buf) - 1);
		}
	}
	if (cs == MICROJXL__CS_CHROMA) {
		strncat(buf, "_", buflen - strlen(buf) - 1);
		if (pr) strncat(buf, pr, buflen - strlen(buf) - 1);
		else {
			char tail[128];
			snprintf(tail, sizeof tail, "%g;%g;%g;%g;%g;%g",
				im->cpoints[1][0], im->cpoints[1][1], im->cpoints[2][0], im->cpoints[2][1],
				im->cpoints[3][0], im->cpoints[3][1]);
			strncat(buf, tail, buflen - strlen(buf) - 1);
		}
	}
	strncat(buf, "_", buflen - strlen(buf) - 1);
	strncat(buf, ri, buflen - strlen(buf) - 1);
	if (cs != MICROJXL__CS_XYB) {
		strncat(buf, "_", buflen - strlen(buf) - 1);
		if (tf) strncat(buf, tf, buflen - strlen(buf) - 1);
		else if (num[0]) strncat(buf, num, buflen - strlen(buf) - 1);
	}
	return buf;
}

/* MaybeCreateProfileImpl. Returns a malloc'd profile (caller frees with
 * microjxl__mem_free) or NULL when the encoding is not representable.
 * Tag order follows libjxl exactly:
 *   desc, cprt, wtpt, [chad], [cicp], [rXYZ gXYZ bXYZ], TRC(s) | A2B0+B2A0
 * (cicp is emitted for known primaries+TF combinations; see below). */
typedef struct { const char *name; size_t offset, size; } microjxl__geniccentry;

static void *microjxl__generate_icc(const microjxl__image_st *im, size_t *out_size) {
	static const float D50_W[3] = {0.964203f, 1.0f, 0.824905f};
	static const float D65[2] = {0.3127f, 0.3290f};
	microjxl__geniccbuf header, tagtable, tags;
	microjxl__geniccentry entries[16];
	int nentries = 0;
	size_t running = 0;
	char desc[256];
	int is_gray = im->cspace == MICROJXL__CS_GREY;
	int is_xyb = im->cspace == MICROJXL__CS_XYB;
	int i;

	*out_size = 0;
	if (im->cspace == MICROJXL__CS_CHROMA && im->gamma_or_tf == MICROJXL__TF_UNKNOWN) return NULL;
	if (is_xyb && im->render_intent != MICROJXL__INTENT_PERC) return NULL;

	/* white point classification (for CICP + tone mapping) */
	static const float DCI_WP[2] = {0.314f, 0.351f};
	int wp_d65 = fabsf(im->cpoints[0][0] - D65[0]) < 3e-5f && fabsf(im->cpoints[0][1] - D65[1]) < 3e-5f;
	int wp_dci = fabsf(im->cpoints[0][0] - DCI_WP[0]) < 3e-5f && fabsf(im->cpoints[0][1] - DCI_WP[1]) < 3e-5f;
	int tf_pq = im->gamma_or_tf == MICROJXL__TF_PQ, tf_hlg = im->gamma_or_tf == MICROJXL__TF_HLG;
	/* primaries classification */
	int pr_srgb = 0, pr_2100 = 0, pr_p3 = 0;
	if (im->cspace == MICROJXL__CS_CHROMA) {
		static const float SRGP[3][2] = {{0.639998686f,0.330010138f},{0.300003784f,0.600003357f},{0.150002046f,0.059997204f}};
		static const float P3P[3][2] = {{0.680f,0.320f},{0.265f,0.690f},{0.150f,0.060f}};
		static const float BT[3][2] = {{0.708f,0.292f},{0.170f,0.797f},{0.131f,0.046f}};
		pr_srgb = pr_2100 = pr_p3 = 1;
		for (i = 0; i < 3; ++i) {
			float x = im->cpoints[1 + i][0], y = im->cpoints[1 + i][1];
			if (fabsf(x - SRGP[i][0]) >= 3e-5f || fabsf(y - SRGP[i][1]) >= 3e-5f) pr_srgb = 0;
			if (fabsf(x - P3P[i][0]) >= 3e-5f || fabsf(y - P3P[i][1]) >= 3e-5f) pr_p3 = 0;
			if (fabsf(x - BT[i][0]) >= 3e-5f || fabsf(y - BT[i][1]) >= 3e-5f) pr_2100 = 0;
		}
	}
	/* MaybeCreateICCCICPTag condition */
	int want_cicp = 0;
	if (im->cspace == MICROJXL__CS_CHROMA && (pr_srgb || pr_2100 || pr_p3)) {
		if (pr_p3 && (wp_d65 || wp_dci)) want_cicp = 1;
		else if (!pr_p3 && wp_d65) want_cicp = 1;
		if (im->gamma_or_tf == MICROJXL__TF_UNKNOWN || im->gamma_or_tf > 0) want_cicp = 0;
	}
	int can_tonemap = 0;
	if (im->cspace == MICROJXL__CS_CHROMA && (tf_pq || tf_hlg)) {
		if ((pr_p3 && (wp_d65 || wp_dci)) || ((!pr_srgb || !pr_2100 || !pr_p3) && wp_d65 && (pr_srgb || pr_2100))) can_tonemap = 1;
		if (pr_srgb || pr_2100) can_tonemap = 1;
		if (!wp_d65 && !(pr_p3 && wp_dci)) can_tonemap = 0;
	}

	memset(&header, 0, sizeof header);
	memset(&tagtable, 0, sizeof tagtable);
	memset(&tags, 0, sizeof tags);

	/* header (CreateICCHeader) */
	if (!microjxl__genicc_put(&header, 0, NULL, 128)) goto fail;
	microjxl__genicc_tag(&header, 4, "jxl ");
	microjxl__genicc_u32(&header, 8, 0x04400000u);
	microjxl__genicc_tag(&header, 12, is_xyb ? "scnr" : "mntr");
	microjxl__genicc_tag(&header, 16, is_gray ? "GRAY" : "RGB ");
	microjxl__genicc_tag(&header, 20, "XYZ ");
	microjxl__genicc_u16(&header, 24, 2019); microjxl__genicc_u16(&header, 26, 12);
	microjxl__genicc_u16(&header, 28, 1); microjxl__genicc_u16(&header, 30, 0);
	microjxl__genicc_u16(&header, 32, 0); microjxl__genicc_u16(&header, 34, 0);
	microjxl__genicc_tag(&header, 36, "acsp");
	microjxl__genicc_tag(&header, 40, "APPL");
	microjxl__genicc_u32(&header, 64, (uint32_t) im->render_intent);
	microjxl__genicc_u32(&header, 68, 0x0000f6d6u);
	microjxl__genicc_u32(&header, 72, 0x00010000u);
	microjxl__genicc_u32(&header, 76, 0x0000d32du);
	microjxl__genicc_tag(&header, 80, "jxl ");
	if (header.err) goto fail;

/* adds one tag entry: name -> the block just finalized in `tags` */
#define MJ_ICC_ADD(nm) do { \
	if (nentries >= 16) goto fail; \
	entries[nentries].name = (nm); entries[nentries].offset = running; \
	entries[nentries].size = tags.size - running; \
	++nentries; \
	running = tags.size; \
} while (0)
#define MJ_ICC_PAD() do { while (tags.size & 3) microjxl__genicc_u8(&tags, tags.size, 0); } while (0)

	/* desc */
	microjxl__genicc_desc(im, desc, sizeof desc);
	if (!microjxl__genicc_mluc(&tags, desc, strlen(desc))) goto fail;
	MJ_ICC_PAD(); MJ_ICC_ADD("desc");
	/* cprt */
	if (!microjxl__genicc_mluc(&tags, "CC0", 3)) goto fail;
	MJ_ICC_PAD(); MJ_ICC_ADD("cprt");
	/* wtpt */
	if (is_gray) {
		float f = 1.0f / im->cpoints[0][1];
		float wt[3] = { im->cpoints[0][0] * f, 1.0f, (1.0f - im->cpoints[0][0] - im->cpoints[0][1]) * f };
		if (!microjxl__genicc_xyz3(&tags, wt)) goto fail;
	} else {
		if (!microjxl__genicc_xyz3(&tags, D50_W)) goto fail;
	}
	MJ_ICC_PAD(); MJ_ICC_ADD("wtpt");
	/* chad */
	if (!is_gray) {
		float chad[3][3];
		if (!microjxl__adapt_to_xyzd50(im->cpoints[0][0], im->cpoints[0][1], chad)) goto fail;
		if (!microjxl__genicc_sf32m(&tags, chad)) goto fail;
		MJ_ICC_PAD(); MJ_ICC_ADD("chad");
		/* cicp */
		if (want_cicp) {
			uint8_t primaries_code = pr_p3 ? (wp_d65 ? 12 : 11) : (uint8_t)(pr_srgb ? 1 : 9);
			size_t pos = tags.size;
			if (!microjxl__genicc_tag(&tags, pos, "cicp") || !microjxl__genicc_u32(&tags, pos + 4, 0)) goto fail;
			if (!microjxl__genicc_u8(&tags, tags.size, (uint8_t) primaries_code)) goto fail;
			if (!microjxl__genicc_u8(&tags, tags.size, (uint8_t)(-im->gamma_or_tf))) goto fail;
			if (!microjxl__genicc_u8(&tags, tags.size, 0) || !microjxl__genicc_u8(&tags, tags.size, 1)) goto fail;
			MJ_ICC_PAD(); MJ_ICC_ADD("cicp");
		}
	}
	/* rXYZ/gXYZ/bXYZ */
	if (im->cspace == MICROJXL__CS_CHROMA) {
		float toxyz[3][3], chad[3][3], m[3][3];
		if (!microjxl__primaries_to_xyz(im->cpoints, toxyz)) goto fail;
		if (!microjxl__adapt_to_xyzd50(im->cpoints[0][0], im->cpoints[0][1], chad)) goto fail;
		microjxl__mul3x3(chad, toxyz, m);
		{
			float r[3] = { m[0][0], m[1][0], m[2][0] };
			float g[3] = { m[0][1], m[1][1], m[2][1] };
			float bl[3] = { m[0][2], m[1][2], m[2][2] };
			if (!microjxl__genicc_xyz3(&tags, r)) goto fail; MJ_ICC_PAD(); MJ_ICC_ADD("rXYZ");
			if (!microjxl__genicc_xyz3(&tags, g)) goto fail; MJ_ICC_PAD(); MJ_ICC_ADD("gXYZ");
			if (!microjxl__genicc_xyz3(&tags, bl)) goto fail; MJ_ICC_PAD(); MJ_ICC_ADD("bXYZ");
		}
	}
	/* TRC / A2B */
	if (is_xyb) {
		if (!microjxl__genicc_xyb_a2b(&tags)) goto fail;
		MJ_ICC_PAD(); MJ_ICC_ADD("A2B0");
		if (!microjxl__genicc_b2a(&tags)) goto fail;
		MJ_ICC_PAD(); MJ_ICC_ADD("B2A0");
	} else if (can_tonemap) {
		static const size_t kDim = 9;
		size_t pos = tags.size;
		if (!microjxl__genicc_tag(&tags, pos, "mft1") || !microjxl__genicc_u32(&tags, pos + 4, 0) ||
			!microjxl__genicc_u8(&tags, pos + 8, 3) || !microjxl__genicc_u8(&tags, pos + 9, 3) ||
			!microjxl__genicc_u8(&tags, pos + 10, (uint8_t) kDim) || !microjxl__genicc_u8(&tags, pos + 11, 0)) goto fail;
		for (i = 0; i < 9; ++i) if (!microjxl__genicc_s15f16(&tags, tags.size, (i % 4 == 0) ? 1.0f : 0.0f)) goto fail;
		for (i = 0; i < 768; ++i) if (!microjxl__genicc_u8(&tags, tags.size, (uint8_t) i)) goto fail;
		{
			int ix, iy, ib;
			for (ix = 0; ix < (int) kDim; ++ix) for (iy = 0; iy < (int) kDim; ++iy) for (ib = 0; ib < (int) kDim; ++ib) {
				float f[3] = { ix * (1.0f / (kDim - 1)), iy * (1.0f / (kDim - 1)), ib * (1.0f / (kDim - 1)) };
				uint8_t lab[3];
				if (!microjxl__icc_tonemap_pixel(im->cpoints, tf_pq, f, lab)) goto fail;
				for (i = 0; i < 3; ++i) if (!microjxl__genicc_u8(&tags, tags.size, lab[i])) goto fail;
			}
		}
		for (i = 768; i < 1536; ++i) if (!microjxl__genicc_u8(&tags, tags.size, (uint8_t)(i - 768))) goto fail;
		MJ_ICC_PAD(); MJ_ICC_ADD("A2B0");
		if (!microjxl__genicc_b2a(&tags)) goto fail;
		MJ_ICC_PAD(); MJ_ICC_ADD("B2A0");
	} else {
		/* TRC: one tag block, registered 1x (gray: kTRC) or 3x (RGB) */
		if (im->gamma_or_tf > 0) {
			float params[1] = { 1.0f / (im->gamma_or_tf * 1e-7f) };
			if (!microjxl__genicc_para(&tags, params, 1, 0)) goto fail;
		} else switch (im->gamma_or_tf) {
			case MICROJXL__TF_HLG: if (!microjxl__genicc_table64(&tags, 0)) goto fail; break;
			case MICROJXL__TF_PQ: if (!microjxl__genicc_table64(&tags, 1)) goto fail; break;
			case MICROJXL__TF_SRGB: case MICROJXL__TF_UNKNOWN: {
				float params[5] = { 2.4f, 1.0f / 1.055f, 0.055f / 1.055f, 1.0f / 12.92f, 0.04045f };
				if (!microjxl__genicc_para(&tags, params, 5, 3)) goto fail;
				break;
			}
			case MICROJXL__TF_709: {
				float params[5] = { 1.0f / 0.45f, 1.0f / 1.099f, 0.099f / 1.099f, 1.0f / 4.5f, 0.081f };
				if (!microjxl__genicc_para(&tags, params, 5, 3)) goto fail;
				break;
			}
			case MICROJXL__TF_LINEAR: {
				float params[5] = { 1.0f, 1.0f, 0.0f, 1.0f, 0.0f };
				if (!microjxl__genicc_para(&tags, params, 5, 3)) goto fail;
				break;
			}
			case MICROJXL__TF_DCI: {
				float params[5] = { 2.6f, 1.0f, 0.0f, 1.0f, 0.0f };
				if (!microjxl__genicc_para(&tags, params, 5, 3)) goto fail;
				break;
			}
			default: goto fail;
		}
		MJ_ICC_PAD();
		/* one tag block registered for all three channels (libjxl adds the
		 * same offset/size three times; gTRC/bTRC must not get size 0) */
		if (is_gray) MJ_ICC_ADD("kTRC");
		else {
			MJ_ICC_ADD("rTRC");
			entries[nentries].name = "gTRC"; entries[nentries].offset = entries[nentries - 1].offset;
			entries[nentries].size = entries[nentries - 1].size; ++nentries;
			entries[nentries].name = "bTRC"; entries[nentries].offset = entries[nentries - 1].offset;
			entries[nentries].size = entries[nentries - 1].size; ++nentries;
		}
	}

	/* assemble: header | tagtable | tags */
	{
		size_t tagtable_size = 4 + 12 * (size_t) nentries;
		size_t total = header.size + tagtable_size + tags.size;
		uint8_t *out = (uint8_t *) MICROJXL_MALLOC(total);
		uint8_t checksum[16];
		size_t pos;
		if (!out) goto fail;
		memcpy(out, header.data, header.size);
		pos = header.size;
		microjxl__genicc_u32buf(out + pos, (uint32_t) nentries);
		for (i = 0; i < nentries; ++i) {
			uint8_t *e = out + pos + 4 + 12 * (size_t) i;
			e[0] = (uint8_t) entries[i].name[0]; e[1] = (uint8_t) entries[i].name[1];
			e[2] = (uint8_t) entries[i].name[2]; e[3] = (uint8_t) entries[i].name[3];
			{
				uint32_t off = (uint32_t)(header.size + tagtable_size + entries[i].offset);
				uint32_t sz = (uint32_t) entries[i].size;
				e[4] = (uint8_t)(off >> 24); e[5] = (uint8_t)(off >> 16); e[6] = (uint8_t)(off >> 8); e[7] = (uint8_t) off;
				e[8] = (uint8_t)(sz >> 24); e[9] = (uint8_t)(sz >> 16); e[10] = (uint8_t)(sz >> 8); e[11] = (uint8_t) sz;
			}
		}
		memcpy(out + pos + tagtable_size, tags.data, tags.size);
		/* size + MD5 with fields 44..48 / 64..68 zeroed */
		{
			uint32_t sz32 = (uint32_t) total;
			out[0] = (uint8_t)(sz32 >> 24); out[1] = (uint8_t)(sz32 >> 16); out[2] = (uint8_t)(sz32 >> 8); out[3] = (uint8_t) sz32;
		}
		{
			uint8_t *tmp = (uint8_t *) MICROJXL_MALLOC(total);
			if (!tmp) { microjxl__mem_free(out); goto fail; }
			memcpy(tmp, out, total);
			memset(tmp + 44, 0, 4);
			memset(tmp + 64, 0, 4);
			microjxl__genicc_md5(tmp, total, checksum);
			microjxl__mem_free(tmp);
		}
		memcpy(out + 84, checksum, 16);
		microjxl__mem_free(header.data);
		microjxl__mem_free(tagtable.data);
		microjxl__mem_free(tags.data);
		*out_size = total;
		return out;
	}
fail:
	microjxl__mem_free(header.data);
	microjxl__mem_free(tagtable.data);
	microjxl__mem_free(tags.data);
	return NULL;
#undef MJ_ICC_ADD
#undef MJ_ICC_PAD
}



/* libjxl CanOutputToColorEncoding (dec_xyb.cc) + OutputEncodingInfo::SetFromMetadata:
 * for an XYB-encoded image whose original colour encoding is not one libjxl can
 * output, the *data* colour encoding becomes ColorEncoding::LinearSRGB(is_gray).
 * HaveFields() is false exactly when the encoding is carried by an ICC, i.e. our
 * `want_icc`; the TF must be one of the representable ones; and a gray encoding
 * must sit on D65. */
static int microjxl__can_output_encoding(const microjxl__image_st *im) {
	int tf = im->gamma_or_tf;
	if (im->want_icc) return 0; /* HaveFields() == false */
	if (!(tf == MICROJXL__TF_PQ || tf == MICROJXL__TF_SRGB || tf == MICROJXL__TF_LINEAR ||
			tf == MICROJXL__TF_HLG || tf == MICROJXL__TF_DCI || tf == MICROJXL__TF_709 || tf > 0)) return 0;
	if (im->cspace == MICROJXL__CS_GREY &&
			!(fabsf(im->cpoints[MICROJXL__CHROMA_WHITE][0] - 0.3127f) < 3e-5f &&
			  fabsf(im->cpoints[MICROJXL__CHROMA_WHITE][1] - 0.3290f) < 3e-5f)) return 0;
	return 1;
}

/* The ICC of the image's *output* colour encoding (libjxl TARGET_DATA).
 * microjxl__generate_icc reads no pointers from the image state, so a shallow
 * copy with substituted fields is enough for the LinearSRGB case. */
static void *microjxl__output_color_encoding_icc(const microjxl__image_st *im, size_t *out_size) {
	*out_size = 0;
	if (im->xyb_encoded && !microjxl__can_output_encoding(im)) {
		static const float SRGP[3][2] = {{0.639998686f,0.330010138f},{0.300003784f,0.600003357f},{0.150002046f,0.059997204f}};
		microjxl__image_st lin = *im;
		lin.cspace = (im->cspace == MICROJXL__CS_GREY) ? MICROJXL__CS_GREY : MICROJXL__CS_CHROMA;
		lin.gamma_or_tf = MICROJXL__TF_LINEAR;
		lin.render_intent = MICROJXL__INTENT_REL;
		lin.want_icc = 0;
		lin.xyb_encoded = 0;
		lin.cpoints[MICROJXL__CHROMA_WHITE][0] = 0.3127f;  lin.cpoints[MICROJXL__CHROMA_WHITE][1] = 0.3290f;
		lin.cpoints[MICROJXL__CHROMA_RED][0]   = SRGP[0][0]; lin.cpoints[MICROJXL__CHROMA_RED][1]   = SRGP[0][1];
		lin.cpoints[MICROJXL__CHROMA_GREEN][0] = SRGP[1][0]; lin.cpoints[MICROJXL__CHROMA_GREEN][1] = SRGP[1][1];
		lin.cpoints[MICROJXL__CHROMA_BLUE][0]  = SRGP[2][0]; lin.cpoints[MICROJXL__CHROMA_BLUE][1]  = SRGP[2][1];
		return microjxl__generate_icc(&lin, out_size);
	}
	/* non-XYB (e.g. JPEG-reconstruction) files keep the original encoding, whose
	 * profile is the embedded ICC itself. */
	if (im->iccsize) {
		void *copy = MICROJXL_MALLOC(im->iccsize);
		if (!copy) return NULL;
		memcpy(copy, im->icc, im->iccsize);
		*out_size = im->iccsize;
		return copy;
	}
	return microjxl__generate_icc(im, out_size);
}

MICROJXL_API const void *microjxl_output_icc_profile(const microjxl_image *image, size_t *size) {
	if (size) *size = 0;
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return NULL;
	if (!image->u.inner->gen_out_icc) {
		image->u.inner->gen_out_icc = microjxl__output_color_encoding_icc(
			&image->u.inner->image, &image->u.inner->gen_out_icc_size);
		if (!image->u.inner->gen_out_icc) image->u.inner->gen_out_icc_size = 0;
	}
	if (size) *size = image->u.inner->gen_out_icc_size;
	return image->u.inner->gen_out_icc;
}

MICROJXL_API const void *microjxl_icc_profile(const microjxl_image *image, size_t *size) {
	if (size) *size = 0;
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return NULL;
	if (image->u.inner->image.iccsize) {
		if (size) *size = image->u.inner->image.iccsize;
		return image->u.inner->image.icc;
	}
	/* enum colour encoding: generate the profile (cached in the inner state) */
	if (!image->u.inner->gen_icc) {
		image->u.inner->gen_icc = (void *) microjxl__generate_icc(&image->u.inner->image, &image->u.inner->gen_icc_size);
		image->u.inner->gen_icc_size = image->u.inner->gen_icc ? image->u.inner->gen_icc_size : 0;
	}
	if (size) *size = image->u.inner->gen_icc_size;
	return image->u.inner->gen_icc;
}

MICROJXL_API const void *microjxl_jpeg_reconstruction(const microjxl_image *image, size_t *size) {
	if (size) *size = 0;
	if (!image || image->magic != MICROJXL__IMAGE_MAGIC || !image->u.inner) return NULL;
	if (size) *size = image->u.inner->jpeg_out_size;
	return image->u.inner->jpeg_out;
}

#endif // defined MICROJXL_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
#endif // MICROJXL__RECURSING < 0                       // internal code ends here //
////////////////////////////////////////////////////////////////////////////////

#if MICROJXL__RECURSING <= 0

#ifdef __cplusplus
}
#endif

#ifdef _MSC_VER
	#pragma warning(pop)
#endif

// prevents double `#include`s---we can't really use `#pragma once` or simple `#ifndef` guards...
#undef MICROJXL__RECURSING
#define MICROJXL__RECURSING 9999

#endif // MICROJXL__RECURSING <= 0

////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// end of file //////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// vim: noet ts=4 st=4 sts=4 sw=4 list colorcolumn=100
