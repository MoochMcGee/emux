#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <bitops.h>
#include <clock.h>
#include <controller.h>
#include <memory.h>
#include <util.h>

#define GP0		0x00
#define GP1		0x04
#define GPUREAD		0x00
#define GPUSTAT		0x04

#define VRAM_SIZE	MB(1)
#define FIFO_SIZE	16
#define FB_W		1024
#define FB_H		512

#define STP_HALF	0
#define STP_ADD		1
#define STP_SUB		2
#define STP_QUARTER	3

#define TEX_4BIT	0
#define TEX_8BIT	1
#define TEX_15BIT	2

struct render_data {
	uint8_t tex_page_x;
	uint8_t tex_page_y;
	uint8_t clut_x;
	uint16_t clut_y;
	uint8_t semi_transparency;
	bool opaque;
	bool textured;
	bool raw;
};

struct pixel {
	int16_t x;
	int16_t y;
	struct color c;
	uint8_t u;
	uint8_t v;
	struct render_data *render_data;
};

struct line {
	int16_t x1;
	int16_t x2;
	int16_t y;
	struct color c1;
	struct color c2;
	uint8_t u1;
	uint8_t u2;
	uint8_t v;
	struct render_data *render_data;
};

struct triangle {
	int16_t x1;
	int16_t y1;
	int16_t x2;
	int16_t y2;
	int16_t x3;
	int16_t y3;
	struct color c1;
	struct color c2;
	struct color c3;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	uint8_t u3;
	uint8_t v3;
	struct render_data *render_data;
};

struct vertex {
	uint32_t x_coord:11;
	uint32_t unused1:5;
	uint32_t y_coord:11;
	uint32_t unused2:5;
};

struct color_attr {
	uint32_t red:8;
	uint32_t green:8;
	uint32_t blue:8;
	uint32_t unused:8;
};

struct tex_page_attr {
	uint16_t x_base:4;
	uint16_t y_base:1;
	uint16_t semi_transparency:2;
	uint16_t colors:2;
	uint16_t unused1:2;
	uint16_t tex_disable:1;
	uint16_t unused2:2;
	uint16_t unused3:2;
};

struct clut_attr {
	uint16_t x_coord:6;
	uint16_t y_coord:9;
	uint16_t unused:1;
};

struct tex_coords {
	uint8_t u;
	uint8_t v;
};

struct coords {
	uint32_t x_pos:16;
	uint32_t y_pos:16;
};

struct dimensions {
	uint32_t width:16;
	uint32_t height:16;
};

union cmd_monochrome_poly {
	uint32_t raw[5];
	struct {
		struct color_attr color;
		struct vertex v1;
		struct vertex v2;
		struct vertex v3;
		struct vertex v4;
	};
};

union cmd_textured_poly {
	uint32_t raw[9];
	struct {
		struct color_attr color;
		struct vertex v1;
		struct {
			struct tex_coords tex_coords1;
			struct clut_attr palette;
		};
		struct vertex v2;
		struct {
			struct tex_coords tex_coords2;
			struct tex_page_attr tex_page;
		};
		struct vertex v3;
		struct {
			struct tex_coords tex_coords3;
			uint16_t unused1;
		};
		struct vertex v4;
		struct {
			struct tex_coords tex_coords4;
			uint16_t unused2;
		};
	};
};

union cmd_shaded_poly {
	uint32_t raw[8];
	struct {
		struct color_attr color1;
		struct vertex v1;
		struct color_attr color2;
		struct vertex v2;
		struct color_attr color3;
		struct vertex v3;
		struct color_attr color4;
		struct vertex v4;
	};
};

union cmd_copy_rect {
	uint32_t raw[3];
	struct {
		uint32_t command;
		struct coords dest;
		struct dimensions dimensions;
	};
};

struct cmd_draw_mode_setting {
	uint32_t x_base:4;
	uint32_t y_base:1;
	uint32_t semi_transparency:2;
	uint32_t colors:2;
	uint32_t dither:1;
	uint32_t drawing_allowed:1;
	uint32_t disable:1;
	uint32_t rect_x_flip:1;
	uint32_t rect_y_flip:1;
	uint32_t unused:10;
	uint32_t opcode:8;
};

struct cmd_tex_window_setting {
	uint32_t mask_x:5;
	uint32_t mask_y:5;
	uint32_t offset_x:5;
	uint32_t offset_y:5;
	uint32_t unused:4;
	uint32_t opcode:8;
};

struct cmd_set_drawing_area {
	uint32_t x_coord:10;
	uint32_t y_coord:10;
	uint32_t unused:4;
	uint32_t opcode:8;
};

struct cmd_set_drawing_offset {
	uint32_t x_offset:11;
	uint32_t y_offset:11;
	uint32_t unused:2;
	uint32_t opcode:8;
};

struct cmd_mask_bit_setting {
	uint32_t set_while_drawing:1;
	uint32_t check_before_draw:1;
	uint32_t unused:22;
	uint32_t opcode:8;
};

struct cmd_dma_dir {
	uint32_t dir:2;
	uint32_t unused:22;
	uint32_t opcode:8;
};

struct cmd_start_of_display_area {
	uint32_t x:10;
	uint32_t y:9;
	uint32_t unused:5;
	uint32_t opcode:8;
};

struct cmd_horizontal_display_range {
	uint32_t x1:12;
	uint32_t x2:12;
	uint32_t opcode:8;
};

struct cmd_vertical_display_range {
	uint32_t y1:10;
	uint32_t y2:10;
	uint32_t unused:4;
	uint32_t opcode:8;
};

struct cmd_display_mode {
	uint32_t horizontal_res_1:2;
	uint32_t vertical_res:1;
	uint32_t video_mode:1;
	uint32_t color_depth:1;
	uint32_t vertical_interlace:1;
	uint32_t horizontal_res_2:1;
	uint32_t reverse:1;
	uint32_t unused:16;
	uint32_t opcode:8;
};

union cmd {
	uint32_t raw;
	struct {
		uint32_t data:24;
		uint32_t opcode:8;
	};
	struct cmd_draw_mode_setting draw_mode_setting;
	struct cmd_tex_window_setting tex_window_setting;
	struct cmd_set_drawing_area set_drawing_area;
	struct cmd_set_drawing_offset set_drawing_offset;
	struct cmd_mask_bit_setting mask_bit_setting;
	struct cmd_dma_dir dma_dir;
	struct cmd_start_of_display_area start_of_display_area;
	struct cmd_horizontal_display_range horizontal_display_range;
	struct cmd_vertical_display_range vertical_display_range;
	struct cmd_display_mode display_mode;
};

struct copy_data {
	uint16_t x;
	uint16_t y;
	uint16_t min_x;
	uint16_t min_y;
	uint16_t max_x;
	uint16_t max_y;
};

union stat {
	uint32_t raw;
	struct {
		uint32_t tex_page_x_base:4;
		uint32_t tex_page_y_base:1;
		uint32_t semi_transparency:2;
		uint32_t tex_page_colors:2;
		uint32_t dither:1;
		uint32_t drawing_allowed:1;
		uint32_t set_mask_bit:1;
		uint32_t draw_pixels:1;
		uint32_t reserved:1;
		uint32_t reverse:1;
		uint32_t tex_disable:1;
		uint32_t horizontal_res_2:1;
		uint32_t horizontal_res_1:2;
		uint32_t vertical_res:1;
		uint32_t video_mode:1;
		uint32_t color_depth:1;
		uint32_t vertical_interlace:1;
		uint32_t display_disable:1;
		uint32_t irq:1;
		uint32_t dma_data_req:1;
		uint32_t ready_recv_cmd:1;
		uint32_t ready_send_vram:1;
		uint32_t ready_recv_dma:1;
		uint32_t dma_dir:2;
		uint32_t odd_even:1;
	};
};

struct fifo {
	uint32_t data[FIFO_SIZE];
	int pos;
	int num;
	bool cmd_in_progress;
	uint8_t cmd_opcode;
	int cmd_word_count;
	int cmd_half_word_count;
};

struct gpu {
	uint8_t vram[VRAM_SIZE];
	union stat stat;
	uint32_t read_buffer;
	uint8_t tex_window_mask_x;
	uint8_t tex_window_mask_y;
	uint8_t tex_window_offset_x;
	uint8_t tex_window_offset_y;
	uint16_t drawing_area_x1;
	uint16_t drawing_area_y1;
	uint16_t drawing_area_x2;
	uint16_t drawing_area_y2;
	uint16_t drawing_offset_x;
	uint16_t drawing_offset_y;
	uint16_t display_area_src_x;
	uint16_t display_area_src_y;
	uint16_t display_area_dest_x1;
	uint16_t display_area_dest_x2;
	uint16_t display_area_dest_y1;
	uint16_t display_area_dest_y2;
	struct copy_data copy_data;
	struct fifo fifo;
	struct region region;
	struct dma_channel dma_channel;
};

static bool gpu_init(struct controller_instance *instance);
static void gpu_reset();
static void gpu_deinit(struct controller_instance *instance);
static uint32_t gpu_readl(struct gpu *gpu, address_t address);
static void gpu_writel(struct gpu *gpu, uint32_t l, address_t address);
static uint32_t gpu_dma_readl(struct gpu *gpu);
static void gpu_dma_writel(struct gpu *gpu, uint32_t l);
static void gpu_process_fifo(struct gpu *gpu);
static void gpu_gp0_cmd(struct gpu *gpu, union cmd cmd);
static void gpu_gp1_cmd(struct gpu *gpu, union cmd cmd);

static void draw_pixel(struct gpu *gpu, struct pixel *pixel);
static void draw_line(struct gpu *gpu, struct line *line);
static void draw_triangle_flat_bottom(struct gpu *gpu, struct triangle *tri);
static void draw_triangle_flat_top(struct gpu *gpu, struct triangle *tri);
static void draw_triangle(struct gpu *gpu, struct triangle *tri);

static inline bool fifo_empty(struct fifo *fifo);
static inline bool fifo_full(struct fifo *fifo);
static inline uint8_t fifo_cmd(struct fifo *fifo);
static inline bool fifo_enqueue(struct fifo *fifo, uint32_t data);
static inline bool fifo_dequeue(struct fifo *fifo, uint32_t *data, int size);

static inline void cmd_clear_cache(struct gpu *gpu);
static inline void cmd_monochrome_3p_poly(struct gpu *gpu, bool opaque);
static inline void cmd_monochrome_4p_poly(struct gpu *gpu, bool opaque);
static inline void cmd_textured_3p_poly(struct gpu *gpu, bool opaque, bool raw);
static inline void cmd_textured_4p_poly(struct gpu *gpu, bool opaque, bool raw);
static inline void cmd_shaded_3p_poly(struct gpu *gpu, bool opaque);
static inline void cmd_shaded_4p_poly(struct gpu *gpu, bool opaque);
static inline void cmd_copy_rect_cpu_to_vram(struct gpu *gpu);
static inline void cmd_copy_rect_vram_to_cpu(struct gpu *gpu);
static inline void cmd_draw_mode_setting(struct gpu *gpu, union cmd cmd);
static inline void cmd_tex_window_setting(struct gpu *gpu);
static inline void cmd_set_drawing_area_tl(struct gpu *gpu, union cmd cmd);
static inline void cmd_set_drawing_area_br(struct gpu *gpu, union cmd cmd);
static inline void cmd_set_drawing_offset(struct gpu *gpu, union cmd cmd);
static inline void cmd_mask_bit_setting(struct gpu *gpu);

static inline void cmd_reset_gpu(struct gpu *gpu);
static inline void cmd_dma_dir(struct gpu *gpu, union cmd cmd);
static inline void cmd_display_mode(struct gpu *gpu, union cmd cmd);

static struct mops gpu_mops = {
	.readl = (readl_t)gpu_readl,
	.writel = (writel_t)gpu_writel
};

static struct dma_ops gpu_dma_ops = {
	.readl = (dma_readl_t)gpu_dma_readl,
	.writel = (dma_writel_t)gpu_dma_writel
};

void draw_pixel(struct gpu *gpu, struct pixel *pixel)
{
	struct render_data *render_data = pixel->render_data;
	struct color dest;
	uint16_t data;
	uint16_t texel;
	uint32_t pixel_off;
	uint32_t clut_off;
	uint32_t tex_off;
	uint8_t index;
	uint16_t r;
	uint16_t g;
	uint16_t b;

	/* Add drawing offset to pixel coordinates */
	pixel->x += gpu->drawing_offset_x;
	pixel->y += gpu->drawing_offset_y;

	/* Discard pixel if it is outside of the clip area */
	if ((pixel->x < gpu->drawing_area_x1) ||
		(pixel->y < gpu->drawing_area_y1) ||
		(pixel->x > gpu->drawing_area_x2) ||
		(pixel->y > gpu->drawing_area_y2))
		return;

	/* Compute pixel offset */
	pixel_off = (pixel->x + pixel->y * FB_W) * sizeof(uint16_t);

	/* Get existing pixel data within frame buffer */
	data = (gpu->vram[pixel_off] << 8) | gpu->vram[pixel_off + 1];

	/* Stop processing if mask out mode is on and pixel is masked (MSB) */
	if (gpu->stat.draw_pixels && bitops_getw(&data, 15, 1))
		return;

	/* Extract color components from existing data */
	dest.r = bitops_getw(&data, 0, 5) << 3;
	dest.g = bitops_getw(&data, 5, 5) << 3;
	dest.b = bitops_getw(&data, 10, 5) << 3;

	/* Check if texture data needs to be sampled */
	if (render_data->textured) {
		/* Set palette offset (unused for 15-bit textures) */
		clut_off = render_data->clut_x * 16;
		clut_off += render_data->clut_y * FB_W;
		clut_off *= sizeof(uint16_t);

		/* Set texel offset based on texture page and V coordinate */
		tex_off = render_data->tex_page_x * 64;
		tex_off += (render_data->tex_page_y * 256 + pixel->v) * FB_W;
		tex_off *= sizeof(uint16_t);

		/* Set final texel offset based on texture bits per pixel */
		switch (gpu->stat.tex_page_colors) {
		case TEX_4BIT:
			/* Update offset (each texel is 4-bit wide) */
			tex_off += (pixel->u / 4) * sizeof(uint16_t);

			/* Set palette index (get appropriate nibble) */
			data = gpu->vram[tex_off] << 8;
			data |= gpu->vram[tex_off + 1];
			index = bitops_getw(&data, (pixel->u % 4) * 4, 4);

			/* Compute final texture offset */
			tex_off = clut_off + index * sizeof(uint16_t);
			break;
		case TEX_8BIT:
			/* Update offset (each texel is 8-bit wide) */
			tex_off += (pixel->u / 2) * sizeof(uint16_t);

			/* Set palette index (get appropriate byte) */
			data = gpu->vram[tex_off] << 8;
			data |= gpu->vram[tex_off + 1];
			index = bitops_getw(&data, (pixel->u % 2) * 8, 8);

			/* Compute final texture offset */
			tex_off = clut_off + index * sizeof(uint16_t);
			break;
		case TEX_15BIT:
			/* Update offset (each texel is 16-bit wide) */
			tex_off += pixel->u * 2;
			break;
		}

		/* Get final texel data and discard pixel if texel is 0 */
		texel = (gpu->vram[tex_off] << 8) | gpu->vram[tex_off + 1];
		if (texel == 0)
			return;

		/* Force opaque state if texel STP bit is not set */
		if (!bitops_getw(&texel, 15, 1))
			render_data->opaque = true;

		/* Extract texel color components */
		r = (bitops_getw(&texel, 0, 5) << 3);
		g = (bitops_getw(&texel, 5, 5) << 3);
		b = (bitops_getw(&texel, 10, 5) << 3);

		/* Blend texel with provided color if needed */
		if (!render_data->raw) {
			/* Blend color (8bit values of 0x80 are brightest and
			values 0x81..0xFF are "brighter than bright" allowing to
			make textures about twice brighter than they are) */
			r = (r * pixel->c.r) / 0x80;
			g = (g * pixel->c.g) / 0x80;
			b = (b * pixel->c.b) / 0x80;

			/* Clamp components to 8-bit */
			pixel->c.r = (r <= 0xFF) ? r : 0xFF;
			pixel->c.g = (g <= 0xFF) ? g : 0xFF;
			pixel->c.b = (b <= 0xFF) ? b : 0xFF;
		}

		/* Update pixel color */
		pixel->c.r = r;
		pixel->c.g = g;
		pixel->c.b = b;
	}

	/* Handle semi-transparency if requested */
	if (!render_data->opaque) {
		switch (render_data->semi_transparency) {
		case STP_HALF:
			break;
		case STP_ADD:
			pixel->c.r += dest.r;
			break;
		case STP_SUB:
			break;
		case STP_QUARTER:
		default:
			break;
		}
	}

	/* Fill masking bit (MSB) according to environment */
	bitops_setw(&data, 15, 1, gpu->stat.set_mask_bit);

	/* Fill color components (discarding lower 3 bits of each color) */
	bitops_setw(&data, 0, 5, pixel->c.r >> 3);
	bitops_setw(&data, 5, 5, pixel->c.g >> 3);
	bitops_setw(&data, 10, 5, pixel->c.b >> 3);

	/* Write data to frame buffer */
	gpu->vram[pixel_off] = data >> 8;
	gpu->vram[pixel_off + 1] = data;
}

void draw_line(struct gpu *gpu, struct line *line)
{
	struct pixel pixel;
	int16_t x;
	struct color c;
	uint8_t u;
	float f;

	/* Copy render data */
	pixel.render_data = line->render_data;

	/* Draw line */
	for (x = line->x1; x < line->x2; x++) {
		/* Get interpolation factor */
		f = (float)(x - line->x1) / (line->x2 - line->x1);

		/* Interpolate color */
		c.r = line->c1.r + f * (line->c2.r - line->c1.r);
		c.g = line->c1.g + f * (line->c2.g - line->c1.g);
		c.b = line->c1.b + f * (line->c2.b - line->c1.b);

		/* Interpolate texture U coordinate */
		u = line->u1 + f * (line->u2 - line->u1);

		/* Build and draw pixel */
		pixel.x = x;
		pixel.y = line->y;
		pixel.c = c;
		pixel.u = u;
		pixel.v = line->v;
		draw_pixel(gpu, &pixel);
	}
}

void draw_triangle_flat_bottom(struct gpu *gpu, struct triangle *triangle)
{
	int16_t x;
	int16_t y;
	struct color c;
	uint8_t u;
	uint8_t v;
	struct line l;
	float x1;
	float x2;
	float x1_inc;
	float x2_inc;
	float t1;
	float t2;
	float t3;
	float t4;
	float f;

	/* Swap second and third vertices if needed */
	if (triangle->x3 < triangle->x2) {
		x = triangle->x2;
		y = triangle->y2;
		c = triangle->c2;
		u = triangle->u2;
		v = triangle->v2;
		triangle->x2 = triangle->x3;
		triangle->y2 = triangle->y3;
		triangle->c2 = triangle->c3;
		triangle->u2 = triangle->u3;
		triangle->v2 = triangle->v3;
		triangle->x3 = x;
		triangle->y3 = y;
		triangle->c3 = c;
		triangle->u3 = u;
		triangle->v3 = v;
	}

	/* Calculate line X increments */
	x1_inc = triangle->x2 - triangle->x1;
	x1_inc /= triangle->y2 - triangle->y1;
	x2_inc = triangle->x3 - triangle->x1;
	x2_inc /= triangle->y3 - triangle->y1;

	/* Initialize X coordinates to first vertex X coordinate */
	x1 = triangle->x1;
	x2 = triangle->x1;

	/* Copy render data */
	l.render_data = triangle->render_data;

	/* Draw triangle */
	for (y = triangle->y1; y < triangle->y2; y++) {
		/* Set line coordinates */
		l.x1 = x1 + 0.5f;
		l.x2 = x2 + 0.5f;
		l.y = y;

		/* Compute first line vertex interpolation factor */
		t1 = triangle->x1 - l.x1;
		t2 = triangle->y1 - y;
		t3 = triangle->x2 - triangle->x1;
		t4 = triangle->y2 - triangle->y1;
		f = sqrtf((t1 * t1) + (t2 * t2)) / sqrtf((t3 * t3) + (t4 * t4));

		/* Compute first vertex color */
		l.c1.r = triangle->c1.r + f * (triangle->c2.r - triangle->c1.r);
		l.c1.g = triangle->c1.g + f * (triangle->c2.g - triangle->c1.g);
		l.c1.b = triangle->c1.b + f * (triangle->c2.b - triangle->c1.b);

		/* Compute first vertex texture U coordinate */
		l.u1 = triangle->u1 + f * (triangle->u2 - triangle->u1);

		/* Compute second line vertex interpolation factor */
		t1 = triangle->x1 - l.x2;
		t2 = triangle->y1 - y;
		t3 = triangle->x3 - triangle->x1;
		t4 = triangle->y3 - triangle->y1;
		f = sqrtf((t1 * t1) + (t2 * t2)) / sqrtf((t3 * t3) + (t4 * t4));

		/* Compute second vertex color */
		l.c2.r = triangle->c1.r + f * (triangle->c3.r - triangle->c1.r);
		l.c2.g = triangle->c1.g + f * (triangle->c3.g - triangle->c1.g);
		l.c2.b = triangle->c1.b + f * (triangle->c3.b - triangle->c1.b);

		/* Compute second vertex texture U coordinate */
		l.u2 = triangle->u1 + f * (triangle->u3 - triangle->u1);

		/* Compute line texture V coordinate */
		f = y - triangle->y1;
		f /= triangle->y2 - triangle->y1;
		l.v = triangle->v1 + f * (triangle->v2 - triangle->v1);

		/* Update X coordinates */
		x1 += x1_inc;
		x2 += x2_inc;

		/* Draw single line */
		draw_line(gpu, &l);
	}
}

void draw_triangle_flat_top(struct gpu *gpu, struct triangle *triangle)
{
	int16_t x;
	int16_t y;
	struct color c;
	uint8_t u;
	uint8_t v;
	struct line l;
	float x1;
	float x2;
	float x1_inc;
	float x2_inc;
	float t1;
	float t2;
	float t3;
	float t4;
	float f;

	/* Swap first and second vertices if needed */
	if (triangle->x2 < triangle->x1) {
		x = triangle->x1;
		y = triangle->y1;
		c = triangle->c1;
		u = triangle->u1;
		v = triangle->v1;
		triangle->x1 = triangle->x2;
		triangle->y1 = triangle->y2;
		triangle->c1 = triangle->c2;
		triangle->u1 = triangle->u2;
		triangle->v1 = triangle->v2;
		triangle->x2 = x;
		triangle->y2 = y;
		triangle->c2 = c;
		triangle->u2 = u;
		triangle->v2 = v;
	}

	/* Calculate line X increments */
	x1_inc = triangle->x1 - triangle->x3;
	x1_inc /= triangle->y1 - triangle->y3;
	x2_inc = triangle->x2 - triangle->x3;
	x2_inc /= triangle->y2 - triangle->y3;

	/* Initialize X coordinates to third vertex X coordinate */
	x1 = triangle->x3;
	x2 = triangle->x3;

	/* Copy render data */
	l.render_data = triangle->render_data;

	/* Draw triangle */
	for (y = triangle->y3; y >= triangle->y1; y--) {
		/* Set line coordinates */
		l.x1 = x1 + 0.5f;
		l.x2 = x2 + 0.5f;
		l.y = y;

		/* Compute first line interpolation factor */
		t1 = triangle->x3 - l.x1;
		t2 = triangle->y3 - y;
		t3 = triangle->x3 - triangle->x1;
		t4 = triangle->y3 - triangle->y1;
		f = sqrtf((t1 * t1) + (t2 * t2)) / sqrtf((t3 * t3) + (t4 * t4));

		/* Compute first vertex color */
		l.c1.r = triangle->c3.r + f * (triangle->c1.r - triangle->c3.r);
		l.c1.g = triangle->c3.g + f * (triangle->c1.g - triangle->c3.g);
		l.c1.b = triangle->c3.b + f * (triangle->c1.b - triangle->c3.b);

		/* Compute second vertex texture U coordinate */
		l.u1 = triangle->u3 + f * (triangle->u1 - triangle->u3);

		/* Compute second line interpolation factor */
		t1 = triangle->x3 - l.x2;
		t2 = triangle->y3 - y;
		t3 = triangle->x3 - triangle->x2;
		t4 = triangle->y3 - triangle->y2;
		f = sqrtf((t1 * t1) + (t2 * t2)) / sqrtf((t3 * t3) + (t4 * t4));

		/* Compute second vertex color */
		l.c2.r = triangle->c3.r + f * (triangle->c2.r - triangle->c3.r);
		l.c2.g = triangle->c3.g + f * (triangle->c2.g - triangle->c3.g);
		l.c2.b = triangle->c3.b + f * (triangle->c2.b - triangle->c3.b);

		/* Compute second vertex texture U coordinate */
		l.u2 = triangle->u3 + f * (triangle->u2 - triangle->u3);

		/* Compute line texture V coordinate */
		f = y - triangle->y1;
		f /= triangle->y3 - triangle->y1;
		l.v = triangle->v1 + f * (triangle->v3 - triangle->v1);

		/* Update X coordinates */
		x1 -= x1_inc;
		x2 -= x2_inc;

		/* Draw single line */
		draw_line(gpu, &l);
	}
}

void draw_triangle(struct gpu *gpu, struct triangle *triangle)
{
	int16_t x;
	int16_t y;
	struct color c;
	uint8_t u;
	uint8_t v;
	struct triangle flat;
	float f;

	/* Swap first and second vertices if needed */
	if (triangle->y2 < triangle->y1) {
		x = triangle->x1;
		y = triangle->y1;
		c = triangle->c1;
		u = triangle->u1;
		v = triangle->v1;
		triangle->x1 = triangle->x2;
		triangle->y1 = triangle->y2;
		triangle->c1 = triangle->c2;
		triangle->u1 = triangle->u2;
		triangle->v1 = triangle->v2;
		triangle->x2 = x;
		triangle->y2 = y;
		triangle->c2 = c;
		triangle->u2 = u;
		triangle->v2 = v;
	}

	/* Swap first and third vertices if needed */
	if (triangle->y3 < triangle->y1) {
		x = triangle->x1;
		y = triangle->y1;
		c = triangle->c1;
		u = triangle->u1;
		v = triangle->v1;
		triangle->x1 = triangle->x3;
		triangle->y1 = triangle->y3;
		triangle->c1 = triangle->c3;
		triangle->u1 = triangle->u3;
		triangle->v1 = triangle->v3;
		triangle->x3 = x;
		triangle->y3 = y;
		triangle->c3 = c;
		triangle->u3 = u;
		triangle->v3 = v;
	}

	/* Swap second and third vertices if needed */
	if (triangle->y3 < triangle->y2) {
		x = triangle->x2;
		y = triangle->y2;
		c = triangle->c2;
		u = triangle->u2;
		v = triangle->v2;
		triangle->x2 = triangle->x3;
		triangle->y2 = triangle->y3;
		triangle->c2 = triangle->c3;
		triangle->u2 = triangle->u3;
		triangle->v2 = triangle->v3;
		triangle->x3 = x;
		triangle->y3 = y;
		triangle->c3 = c;
		triangle->u3 = u;
		triangle->v3 = v;
	}

	/* Draw flat bottom triangle and return if possible */
	if (triangle->y2 == triangle->y3) {
		draw_triangle_flat_bottom(gpu, triangle);
		return;
	}

	/* Draw flat top triangle and return if possible */
	if (triangle->y1 == triangle->y2) {
		draw_triangle_flat_top(gpu, triangle);
		return;
	}

	/* Compute new vertex */
	f = triangle->y2 - triangle->y1;
	f /= triangle->y3 - triangle->y1;
	x = triangle->x1 + f * (triangle->x3 - triangle->x1);
	y = triangle->y2;
	c.r = triangle->c1.r + f * (triangle->c3.r - triangle->c1.r);
	c.g = triangle->c1.g + f * (triangle->c3.g - triangle->c1.g);
	c.b = triangle->c1.b + f * (triangle->c3.b - triangle->c1.b);

	/* Copy render data */
	flat.render_data = triangle->render_data;

	/* Create flat bottom triangle and draw it */
	flat.x1 = triangle->x1;
	flat.y1 = triangle->y1;
	flat.c1 = triangle->c1;
	flat.x2 = triangle->x2;
	flat.y2 = triangle->y2;
	flat.c2 = triangle->c2;
	flat.x3 = x;
	flat.y3 = y;
	flat.c3 = c;
	draw_triangle_flat_bottom(gpu, &flat);

	/* Create flat top triangle and draw it */
	flat.x1 = triangle->x2;
	flat.y1 = triangle->y2;
	flat.c1 = triangle->c2;
	flat.x2 = x;
	flat.y2 = y;
	flat.c2 = c;
	flat.x3 = triangle->x3;
	flat.y3 = triangle->y3;
	flat.c3 = triangle->c3;
	draw_triangle_flat_top(gpu, &flat);
}

/* GP0(00h) - NOP
   GP0(04h..1Eh,E0h,E7h..EFh) - Mirrors of GP0(00h) - NOP */
void cmd_nop()
{
}

/* GP0(01h) - Clear Cache */
void cmd_clear_cache(struct gpu *gpu)
{
	/* Reset command buffer */
	memset(&gpu->fifo, 0, sizeof(struct fifo));

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(20h) - Monochrome three-point polygon, opaque
   GP0(22h) - Monochrome three-point polygon, semi-transparent */
void cmd_monochrome_3p_poly(struct gpu *gpu, bool opaque)
{
	union cmd_monochrome_poly cmd;
	struct render_data render_data;
	struct triangle triangle;
	struct color c;

	/* Dequeue FIFO (monochrome 3-point polygon needs 4 arguments) */
	if (!fifo_dequeue(&gpu->fifo, cmd.raw, 4))
		return;

	/* Extract color from command */
	c.r = cmd.color.red;
	c.g = cmd.color.green;
	c.b = cmd.color.blue;

	/* Set render data */
	render_data.opaque = opaque;
	render_data.textured = false;
	triangle.render_data = &render_data;

	/* Set semi-transparency mode from status register if needed */
	if (!opaque)
		render_data.semi_transparency = gpu->stat.semi_transparency;

	/* Build and draw triangle */
	triangle.x1 = cmd.v1.x_coord;
	triangle.y1 = cmd.v1.y_coord;
	triangle.c1 = c;
	triangle.x2 = cmd.v2.x_coord;
	triangle.y2 = cmd.v2.y_coord;
	triangle.c2 = c;
	triangle.x3 = cmd.v3.x_coord;
	triangle.y3 = cmd.v3.y_coord;
	triangle.c3 = c;
	draw_triangle(gpu, &triangle);

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(28h) - Monochrome four-point polygon, opaque
   GP0(2Ah) - Monochrome four-point polygon, semi-transparent */
void cmd_monochrome_4p_poly(struct gpu *gpu, bool opaque)
{
	union cmd_monochrome_poly cmd;
	struct render_data render_data;
	struct triangle triangle;
	struct color c;

	/* Dequeue FIFO (monochrome 4-point polygon needs 5 arguments) */
	if (!fifo_dequeue(&gpu->fifo, cmd.raw, 5))
		return;

	/* Extract color from command */
	c.r = cmd.color.red;
	c.g = cmd.color.green;
	c.b = cmd.color.blue;

	/* Set render data */
	render_data.opaque = opaque;
	render_data.textured = false;
	triangle.render_data = &render_data;

	/* Set semi-transparency mode from status register if needed */
	if (!opaque)
		render_data.semi_transparency = gpu->stat.semi_transparency;

	/* Build and draw first triangle */
	triangle.x1 = cmd.v1.x_coord;
	triangle.y1 = cmd.v1.y_coord;
	triangle.c1 = c;
	triangle.x2 = cmd.v2.x_coord;
	triangle.y2 = cmd.v2.y_coord;
	triangle.c2 = c;
	triangle.x3 = cmd.v3.x_coord;
	triangle.y3 = cmd.v3.y_coord;
	triangle.c3 = c;
	draw_triangle(gpu, &triangle);

	/* Build and draw second triangle */
	triangle.x1 = cmd.v2.x_coord;
	triangle.y1 = cmd.v2.y_coord;
	triangle.c1 = c;
	triangle.x2 = cmd.v3.x_coord;
	triangle.y2 = cmd.v3.y_coord;
	triangle.c2 = c;
	triangle.x3 = cmd.v4.x_coord;
	triangle.y3 = cmd.v4.y_coord;
	triangle.c3 = c;
	draw_triangle(gpu, &triangle);

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(24h) - Textured three-point polygon, opaque, texture-blending
   GP0(25h) - Textured three-point polygon, opaque, raw-texture
   GP0(26h) - Textured three-point polygon, semi-transparent, texture-blending
   GP0(27h) - Textured three-point polygon, semi-transparent, raw-texture */
void cmd_textured_3p_poly(struct gpu *gpu, bool opaque, bool raw)
{
	union cmd_textured_poly cmd;
	struct render_data render_data;
	struct triangle triangle;
	struct color c;

	/* Dequeue FIFO (textured 3-point polygon needs 7 arguments) */
	if (!fifo_dequeue(&gpu->fifo, cmd.raw, 7))
		return;

	/* Extract color from command */
	c.r = cmd.color.red;
	c.g = cmd.color.green;
	c.b = cmd.color.blue;

	/* Set render data */
	render_data.opaque = opaque;
	render_data.textured = true;
	render_data.raw = raw;
	render_data.tex_page_x = cmd.tex_page.x_base;
	render_data.tex_page_y = cmd.tex_page.y_base;
	render_data.clut_x = cmd.palette.x_coord;
	render_data.clut_y = cmd.palette.y_coord;
	triangle.render_data = &render_data;

	/* Set semi-transparency mode from texture page if needed */
	if (!opaque)
		render_data.semi_transparency = cmd.tex_page.semi_transparency;

	/* Build and draw triangle */
	triangle.x1 = cmd.v1.x_coord;
	triangle.y1 = cmd.v1.y_coord;
	triangle.c1 = c;
	triangle.u1 = cmd.tex_coords1.u;
	triangle.v1 = cmd.tex_coords1.v;
	triangle.x2 = cmd.v2.x_coord;
	triangle.y2 = cmd.v2.y_coord;
	triangle.c2 = c;
	triangle.u2 = cmd.tex_coords2.u;
	triangle.v2 = cmd.tex_coords2.v;
	triangle.x3 = cmd.v3.x_coord;
	triangle.y3 = cmd.v3.y_coord;
	triangle.c3 = c;
	triangle.u3 = cmd.tex_coords3.u;
	triangle.v3 = cmd.tex_coords3.v;
	draw_triangle(gpu, &triangle);

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(2Ch) - Textured four-point polygon, opaque, texture-blending
   GP0(2Dh) - Textured four-point polygon, opaque, raw-texture
   GP0(2Eh) - Textured four-point polygon, semi-transparent, texture-blending
   GP0(2Fh) - Textured four-point polygon, semi-transparent, raw-texture */
void cmd_textured_4p_poly(struct gpu *gpu, bool opaque, bool raw)
{
	union cmd_textured_poly cmd;
	struct render_data render_data;
	struct triangle triangle;
	struct color c;

	/* Dequeue FIFO (textured 4-point polygon needs 9 arguments) */
	if (!fifo_dequeue(&gpu->fifo, cmd.raw, 9))
		return;

	/* Extract color from command */
	c.r = cmd.color.red;
	c.g = cmd.color.green;
	c.b = cmd.color.blue;

	/* Set render data */
	render_data.opaque = opaque;
	render_data.textured = true;
	render_data.raw = raw;
	render_data.tex_page_x = cmd.tex_page.x_base;
	render_data.tex_page_y = cmd.tex_page.y_base;
	render_data.clut_x = cmd.palette.x_coord;
	render_data.clut_y = cmd.palette.y_coord;
	triangle.render_data = &render_data;

	/* Set semi-transparency mode from texture page if needed */
	if (!opaque)
		render_data.semi_transparency = cmd.tex_page.semi_transparency;

	/* Build and draw first triangle */
	triangle.x1 = cmd.v1.x_coord;
	triangle.y1 = cmd.v1.y_coord;
	triangle.c1 = c;
	triangle.u1 = cmd.tex_coords1.u;
	triangle.v1 = cmd.tex_coords1.v;
	triangle.x2 = cmd.v2.x_coord;
	triangle.y2 = cmd.v2.y_coord;
	triangle.c2 = c;
	triangle.u2 = cmd.tex_coords2.u;
	triangle.v2 = cmd.tex_coords2.v;
	triangle.x3 = cmd.v3.x_coord;
	triangle.y3 = cmd.v3.y_coord;
	triangle.c3 = c;
	triangle.u3 = cmd.tex_coords3.u;
	triangle.v3 = cmd.tex_coords3.v;
	draw_triangle(gpu, &triangle);

	/* Build and draw second triangle */
	triangle.x1 = cmd.v2.x_coord;
	triangle.y1 = cmd.v2.y_coord;
	triangle.c1 = c;
	triangle.u1 = cmd.tex_coords2.u;
	triangle.v1 = cmd.tex_coords2.v;
	triangle.x2 = cmd.v3.x_coord;
	triangle.y2 = cmd.v3.y_coord;
	triangle.c2 = c;
	triangle.u2 = cmd.tex_coords3.u;
	triangle.v2 = cmd.tex_coords3.v;
	triangle.x3 = cmd.v4.x_coord;
	triangle.y3 = cmd.v4.y_coord;
	triangle.c3 = c;
	triangle.u3 = cmd.tex_coords4.u;
	triangle.v3 = cmd.tex_coords4.v;
	draw_triangle(gpu, &triangle);

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(30h) - Shaded three-point polygon, opaque
   GP0(32h) - Shaded three-point polygon, semi-transparent */
void cmd_shaded_3p_poly(struct gpu *gpu, bool opaque)
{
	union cmd_shaded_poly cmd;
	struct render_data render_data;
	struct triangle triangle;

	/* Dequeue FIFO (shaded 3-point polygon needs 6 arguments) */
	if (!fifo_dequeue(&gpu->fifo, cmd.raw, 6))
		return;

	/* Set render data */
	render_data.opaque = opaque;
	render_data.textured = false;
	triangle.render_data = &render_data;

	/* Set semi-transparency mode from status register if needed */
	if (!opaque)
		render_data.semi_transparency = gpu->stat.semi_transparency;

	/* Build and draw triangle */
	triangle.x1 = cmd.v1.x_coord;
	triangle.y1 = cmd.v1.y_coord;
	triangle.c1.r = cmd.color1.red;
	triangle.c1.g = cmd.color1.green;
	triangle.c1.b = cmd.color1.blue;
	triangle.x2 = cmd.v2.x_coord;
	triangle.y2 = cmd.v2.y_coord;
	triangle.c2.r = cmd.color2.red;
	triangle.c2.g = cmd.color2.green;
	triangle.c2.b = cmd.color2.blue;
	triangle.x3 = cmd.v3.x_coord;
	triangle.y3 = cmd.v3.y_coord;
	triangle.c3.r = cmd.color3.red;
	triangle.c3.g = cmd.color3.green;
	triangle.c3.b = cmd.color3.blue;
	draw_triangle(gpu, &triangle);

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(38h) - Shaded four-point polygon, opaque
   GP0(3Ah) - Shaded four-point polygon, semi-transparent */
void cmd_shaded_4p_poly(struct gpu *gpu, bool opaque)
{
	union cmd_shaded_poly cmd;
	struct render_data render_data;
	struct triangle triangle;

	/* Dequeue FIFO (shaded 4-point polygon needs 8 arguments) */
	if (!fifo_dequeue(&gpu->fifo, cmd.raw, 8))
		return;

	/* Set render data */
	render_data.opaque = opaque;
	render_data.textured = false;
	triangle.render_data = &render_data;

	/* Set semi-transparency mode from status register if needed */
	if (!opaque)
		render_data.semi_transparency = gpu->stat.semi_transparency;

	/* Build and draw first triangle */
	triangle.x1 = cmd.v1.x_coord;
	triangle.y1 = cmd.v1.y_coord;
	triangle.c1.r = cmd.color1.red;
	triangle.c1.g = cmd.color1.green;
	triangle.c1.b = cmd.color1.blue;
	triangle.x2 = cmd.v2.x_coord;
	triangle.y2 = cmd.v2.y_coord;
	triangle.c2.r = cmd.color2.red;
	triangle.c2.g = cmd.color2.green;
	triangle.c2.b = cmd.color2.blue;
	triangle.x3 = cmd.v3.x_coord;
	triangle.y3 = cmd.v3.y_coord;
	triangle.c3.r = cmd.color3.red;
	triangle.c3.g = cmd.color3.green;
	triangle.c3.b = cmd.color3.blue;
	draw_triangle(gpu, &triangle);

	/* Build and draw second triangle */
	triangle.x1 = cmd.v2.x_coord;
	triangle.y1 = cmd.v2.y_coord;
	triangle.c1.r = cmd.color2.red;
	triangle.c1.g = cmd.color2.green;
	triangle.c1.b = cmd.color2.blue;
	triangle.x2 = cmd.v3.x_coord;
	triangle.y2 = cmd.v3.y_coord;
	triangle.c2.r = cmd.color3.red;
	triangle.c2.g = cmd.color3.green;
	triangle.c2.b = cmd.color3.blue;
	triangle.x3 = cmd.v4.x_coord;
	triangle.y3 = cmd.v4.y_coord;
	triangle.c3.r = cmd.color4.red;
	triangle.c3.g = cmd.color4.green;
	triangle.c3.b = cmd.color4.blue;
	draw_triangle(gpu, &triangle);

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(A0h) - Copy Rectangle (CPU to VRAM) */
void cmd_copy_rect_cpu_to_vram(struct gpu *gpu)
{
	union cmd_copy_rect cmd;
	bool decoded;
	int count;
	uint32_t data;
	uint32_t offset;
	uint16_t half_word;
	int i;

	/* Check if command is decoded (set once no words are left) */
	decoded = (gpu->fifo.cmd_word_count > 0);

	/* Retrieve command parameters if needed (3 arguments) */
	if (!decoded && !fifo_dequeue(&gpu->fifo, cmd.raw, 3))
		return;

	/* Set word count based on dimensions (handling padding) if needed */
	if (gpu->fifo.cmd_word_count == 0) {
		/* Save copy data */
		gpu->copy_data.x = cmd.dest.x_pos;
		gpu->copy_data.y = cmd.dest.y_pos;
		gpu->copy_data.min_x = cmd.dest.x_pos;
		gpu->copy_data.min_y = cmd.dest.y_pos;
		gpu->copy_data.max_x = cmd.dest.x_pos + cmd.dimensions.width;
		gpu->copy_data.max_y = cmd.dest.y_pos + cmd.dimensions.height;

		/* Get command word count based on pixel count */
		count = cmd.dimensions.width * cmd.dimensions.height;
		gpu->fifo.cmd_word_count = (count + 1) / 2;
	}

	/* Dequeue data */
	if (fifo_dequeue(&gpu->fifo, &data, 1)) {
		/* Copy two half words (data contains two pixels) */
		for (i = 0; i < 2; i++) {
			/* Get appropriate half word */
			half_word = ((i % 2) == 0) ? data & 0xFFFF : data >> 16;

			/* Compute destination offset in frame buffer */
			offset = gpu->copy_data.x;
			offset += gpu->copy_data.y * FB_W;
			offset *= sizeof(uint16_t);

			/* Write bytes to frame buffer */
			gpu->vram[offset] = half_word >> 8;
			gpu->vram[offset + 1] = half_word;

			/* Update destination coordinates (handling bounds) */
			if (++gpu->copy_data.x == gpu->copy_data.max_x) {
				gpu->copy_data.x = gpu->copy_data.min_x;
				if (++gpu->copy_data.y == gpu->copy_data.max_y)
					break;
			}
		}

		/* Decrement word count and flag command as complete */
		if (--gpu->fifo.cmd_word_count == 0)
			gpu->fifo.cmd_in_progress = false;
	}
}

/* GP0(C0h) - Copy Rectangle (VRAM to CPU) */
void cmd_copy_rect_vram_to_cpu(struct gpu *gpu)
{
	union cmd_copy_rect cmd;
	//int hw_count;

	/* Retrieve command parameters (3 arguments) */
	if (!fifo_dequeue(&gpu->fifo, cmd.raw, 3))
		return;

	/* Set half word count based on dimensions */
	//hw_count = cmd.dimensions.width * cmd.dimensions.height;

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(E1h) - Draw Mode setting (aka "Texpage") */
void cmd_draw_mode_setting(struct gpu *gpu, union cmd cmd)
{
	/* Save drawing mode parameters */
	gpu->stat.tex_page_x_base = cmd.draw_mode_setting.x_base;
	gpu->stat.tex_page_y_base = cmd.draw_mode_setting.y_base;
	gpu->stat.semi_transparency = cmd.draw_mode_setting.semi_transparency;
	gpu->stat.tex_page_colors = cmd.draw_mode_setting.colors;
	gpu->stat.dither = cmd.draw_mode_setting.dither;
	gpu->stat.drawing_allowed = cmd.draw_mode_setting.drawing_allowed;
	gpu->stat.tex_disable = cmd.draw_mode_setting.disable;
}

/* GP0(E2h) - Texture Window setting */
void cmd_tex_window_setting(struct gpu *gpu)
{
	union cmd cmd;

	/* Dequeue command */
	fifo_dequeue(&gpu->fifo, &cmd.raw, 1);

	/* Save texture window parameters */
	gpu->tex_window_mask_x = cmd.tex_window_setting.mask_x;
	gpu->tex_window_mask_y = cmd.tex_window_setting.mask_y;
	gpu->tex_window_offset_x = cmd.tex_window_setting.offset_x;
	gpu->tex_window_offset_y = cmd.tex_window_setting.offset_y;

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP0(E3h) - Set Drawing Area top left (X1,Y1) */
void cmd_set_drawing_area_tl(struct gpu *gpu, union cmd cmd)
{
	/* Update top-left X/Y drawing area coordinates */
	gpu->drawing_area_x1 = cmd.set_drawing_area.x_coord;
	gpu->drawing_area_y1 = cmd.set_drawing_area.y_coord;
}

/* GP0(E4h) - Set Drawing Area bottom right (X2,Y2) */
void cmd_set_drawing_area_br(struct gpu *gpu, union cmd cmd)
{
	/* Update bottom-right X/Y drawing area coordinates */
	gpu->drawing_area_x2 = cmd.set_drawing_area.x_coord;
	gpu->drawing_area_y2 = cmd.set_drawing_area.y_coord;
}

/* GP0(E5h) - Set Drawing Offset (X,Y) */
void cmd_set_drawing_offset(struct gpu *gpu, union cmd cmd)
{
	/* Update X/Y drawing offset */
	gpu->drawing_offset_x = (int16_t)cmd.set_drawing_offset.x_offset;
	gpu->drawing_offset_y = (int16_t)cmd.set_drawing_offset.y_offset;
}

/* GP0(E6h) - Mask Bit Setting */
void cmd_mask_bit_setting(struct gpu *gpu)
{
	union cmd cmd;

	/* Dequeue command */
	fifo_dequeue(&gpu->fifo, &cmd.raw, 1);

	/* Save mask bit parameters */
	gpu->stat.set_mask_bit = cmd.mask_bit_setting.set_while_drawing;
	gpu->stat.draw_pixels = cmd.mask_bit_setting.check_before_draw;

	/* Flag command as complete */
	gpu->fifo.cmd_in_progress = false;
}

/* GP1(00h) - Reset GPU */
void cmd_reset_gpu(struct gpu *gpu)
{
	/* Reset status register */
	gpu->stat.raw = 0;
	gpu->stat.reserved = 1;
	gpu->stat.display_disable = 0;

	/* Reset command buffer */
	memset(&gpu->fifo, 0, sizeof(struct fifo));

	/* GPU is always ready to send/receive commands */
	gpu->stat.ready_recv_cmd = 1;
	gpu->stat.ready_send_vram = 1;
	gpu->stat.ready_recv_dma = 1;
}

/* GP1(04h) - DMA Direction / Data Request */
void cmd_dma_dir(struct gpu *gpu, union cmd cmd)
{
	/* Save DMA direction */
	gpu->stat.dma_dir = cmd.dma_dir.dir;
}

/* GP1(05h) - Start of Display area (in VRAM) */
void cmd_start_of_display_area(struct gpu *gpu, union cmd cmd)
{
	/* Save display area origin */
	gpu->display_area_src_x = cmd.start_of_display_area.x;
	gpu->display_area_src_y = cmd.start_of_display_area.y;
}

/* GP1(06h) - Horizontal Display range (on Screen) */
void cmd_horizontal_display_range(struct gpu *gpu, union cmd cmd)
{
	/* Save horizontal display range */
	gpu->display_area_dest_x1 = cmd.horizontal_display_range.x1;
	gpu->display_area_dest_x2 = cmd.horizontal_display_range.x2;
}

/* GP1(07h) - Vertical Display range (on Screen) */
void cmd_vertical_display_range(struct gpu *gpu, union cmd cmd)
{
	/* Save horizontal display range */
	gpu->display_area_dest_y1 = cmd.vertical_display_range.y1;
	gpu->display_area_dest_y2 = cmd.vertical_display_range.y2;
}

/* GP1(08h) - Display mode */
void cmd_display_mode(struct gpu *gpu, union cmd cmd)
{
	/* Save parameters */
	gpu->stat.horizontal_res_1 = cmd.display_mode.horizontal_res_1;
	gpu->stat.vertical_res = cmd.display_mode.vertical_res;
	gpu->stat.video_mode = cmd.display_mode.video_mode;
	gpu->stat.color_depth = cmd.display_mode.color_depth;
	gpu->stat.vertical_interlace = cmd.display_mode.vertical_interlace;
	gpu->stat.horizontal_res_2 = cmd.display_mode.horizontal_res_2;
	gpu->stat.reverse = cmd.display_mode.reverse;
}

bool fifo_empty(struct fifo *fifo)
{
	/* Return whether if FIFO is empty or not */
	return (fifo->num == 0);
}

bool fifo_full(struct fifo *fifo)
{
	/* Return whether if FIFO is full or not */
	return (fifo->num == FIFO_SIZE);
}

uint8_t fifo_cmd(struct fifo *fifo)
{
	union cmd cmd;
	int index;

	/* Return command opcode in original enquued element */
	index = ((fifo->pos - fifo->num) + FIFO_SIZE) % FIFO_SIZE;
	cmd.raw = fifo->data[index];
	return cmd.opcode;
}

bool fifo_enqueue(struct fifo *fifo, uint32_t data)
{
	/* Return already if FIFO is full */
	if (fifo_full(fifo))
		return false;

	/* Add command/data to FIFO and handle position overflow */
	fifo->data[fifo->pos++] = data;
	if (fifo->pos == FIFO_SIZE)
		fifo->pos = 0;

	/* Increment number of elements */
	fifo->num++;

	/* Return success */
	return true;
}

bool fifo_dequeue(struct fifo *fifo, uint32_t *data, int size)
{
	int index;
	int i;

	/* Return if FIFO does not have enough elements */
	if (fifo->num < size)
		return false;

	/* Remove command/data from FIFO */
	for (i = 0; i < size; i++) {
		index = ((fifo->pos - fifo->num) + FIFO_SIZE) % FIFO_SIZE;
		data[i] = fifo->data[index];
		fifo->num--;
	}

	/* Return success */
	return true;
}

uint32_t gpu_readl(struct gpu *gpu, address_t address)
{
	/* Handle read */
	switch (address) {
	case GPUREAD:
		return 0;
	case GPUSTAT:
	default:
		return gpu->stat.raw;
	}
}

void gpu_writel(struct gpu *gpu, uint32_t l, address_t address)
{
	union cmd cmd;

	/* Capture raw command */
	cmd.raw = l;

	/* Call appropriate command handler based on address */
	switch (address) {
	case GP0:
		gpu_gp0_cmd(gpu, cmd);
		break;
	case GP1:
	default:
		gpu_gp1_cmd(gpu, cmd);
		break;
	}
}

uint32_t gpu_dma_readl(struct gpu *gpu)
{
	/* Consume a single clock cycle */
	clock_consume(1);

	/* DMA operation is equivalent to accessing GPUREAD by software */
	return gpu_readl(gpu, GPUREAD);
}

void gpu_dma_writel(struct gpu *gpu, uint32_t l)
{
	/* Consume a single clock cycle */
	clock_consume(1);

	/* DMA operation is equivalent to accessing GP0 by software */
	gpu_writel(gpu, l, GP0);
}

void gpu_process_fifo(struct gpu *gpu)
{
	struct fifo *fifo = &gpu->fifo;

	/* Check if command is not being processed already */
	if (!fifo->cmd_in_progress) {
		/* Return if FIFO is empty */
		if (fifo_empty(fifo))
			return;

		/* Retrieve command opcode */
		fifo->cmd_opcode = fifo_cmd(fifo);

		/* Flag command as in progress */
		fifo->cmd_in_progress = true;

		/* Reset number of remaining words */
		fifo->cmd_word_count = 0;
	}

	/* Handle GP0 command */
	switch (fifo->cmd_opcode) {
	case 0x01:
		cmd_clear_cache(gpu);
		break;
	case 0x20:
		cmd_monochrome_3p_poly(gpu, true);
		break;
	case 0x22:
		cmd_monochrome_3p_poly(gpu, false);
		break;
	case 0x24:
		cmd_textured_3p_poly(gpu, true, false);
		break;
	case 0x25:
		cmd_textured_3p_poly(gpu, true, true);
		break;
	case 0x26:
		cmd_textured_3p_poly(gpu, false, false);
		break;
	case 0x27:
		cmd_textured_3p_poly(gpu, false, true);
		break;
	case 0x28:
		cmd_monochrome_4p_poly(gpu, true);
		break;
	case 0x2A:
		cmd_monochrome_4p_poly(gpu, false);
		break;
	case 0x2C:
		cmd_textured_4p_poly(gpu, true, false);
		break;
	case 0x2D:
		cmd_textured_4p_poly(gpu, true, true);
		break;
	case 0x2E:
		cmd_textured_4p_poly(gpu, false, false);
		break;
	case 0x2F:
		cmd_textured_4p_poly(gpu, false, true);
		break;
	case 0x30:
		cmd_shaded_3p_poly(gpu, true);
		break;
	case 0x32:
		cmd_shaded_3p_poly(gpu, false);
		break;
	case 0x38:
		cmd_shaded_4p_poly(gpu, true);
		break;
	case 0x3A:
		cmd_shaded_4p_poly(gpu, false);
		break;
	case 0xA0:
		cmd_copy_rect_cpu_to_vram(gpu);
		break;
	case 0xC0:
		cmd_copy_rect_vram_to_cpu(gpu);
		break;
	case 0xE2:
		cmd_tex_window_setting(gpu);
		break;
	case 0xE6:
		cmd_mask_bit_setting(gpu);
		break;
	default:
		LOG_W("Unhandled GP0 opcode (%02x)!\n", fifo->cmd_opcode);
		break;
	}
}

void gpu_gp0_cmd(struct gpu *gpu, union cmd cmd)
{
	/* Handle immediate commands and leave if no command is in progress */
	if (!gpu->fifo.cmd_in_progress)
		switch (cmd.opcode) {
		case 0x00:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x0C:
		case 0x0D:
		case 0x0E:
		case 0x0F:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x1C:
		case 0x1D:
		case 0x1E:
		case 0xE0:
		case 0xE7:
		case 0xE8:
		case 0xE9:
		case 0xEA:
		case 0xEB:
		case 0xEC:
		case 0xED:
		case 0xEE:
		case 0xEF:
			cmd_nop();
			return;
		case 0xE1:
			cmd_draw_mode_setting(gpu, cmd);
			return;
		case 0xE3:
			cmd_set_drawing_area_tl(gpu, cmd);
			return;
		case 0xE4:
			cmd_set_drawing_area_br(gpu, cmd);
			return;
		case 0xE5:
			cmd_set_drawing_offset(gpu, cmd);
			return;
		default:
			break;
		}

	/* Add command/data to FIFO */
	fifo_enqueue(&gpu->fifo, cmd.raw);

	/* Process FIFO commands */
	gpu_process_fifo(gpu);
}

void gpu_gp1_cmd(struct gpu *gpu, union cmd cmd)
{
	/* Execute command */
	switch (cmd.opcode) {
	case 0x00:
		cmd_reset_gpu(gpu);
		break;
	case 0x04:
		cmd_dma_dir(gpu, cmd);
		break;
	case 0x05:
		cmd_start_of_display_area(gpu, cmd);
		break;
	case 0x06:
		cmd_horizontal_display_range(gpu, cmd);
		break;
	case 0x07:
		cmd_vertical_display_range(gpu, cmd);
		break;
	case 0x08:
		cmd_display_mode(gpu, cmd);
		break;
	default:
		LOG_W("Unhandled GP1 opcode (%02x)!\n", cmd.opcode);
		break;
	}
}

bool gpu_init(struct controller_instance *instance)
{
	struct gpu *gpu;
	struct resource *res;

	/* Allocate GPU structure */
	instance->priv_data = calloc(1, sizeof(struct gpu));
	gpu = instance->priv_data;

	/* Add GPU memory region */
	res = resource_get("mem",
		RESOURCE_MEM,
		instance->resources,
		instance->num_resources);
	gpu->region.area = res;
	gpu->region.mops = &gpu_mops;
	gpu->region.data = gpu;
	memory_region_add(&gpu->region);

	/* Add GPU DMA channel */
	res = resource_get("dma",
		RESOURCE_DMA,
		instance->resources,
		instance->num_resources);
	gpu->dma_channel.res = res;
	gpu->dma_channel.ops = &gpu_dma_ops;
	gpu->dma_channel.data = gpu;
	dma_channel_add(&gpu->dma_channel);

	return true;
}

void gpu_reset(struct controller_instance *instance)
{
	struct gpu *gpu = instance->priv_data;

	/* Reset private data */
	memset(gpu->vram, 0, VRAM_SIZE);
	gpu->stat.raw = 0;
	gpu->read_buffer = 0;
	gpu->tex_window_mask_x = 0;
	gpu->tex_window_mask_y = 0;
	gpu->tex_window_offset_x = 0;
	gpu->tex_window_offset_y = 0;
	gpu->drawing_area_x1 = 0;
	gpu->drawing_area_y1 = 0;
	gpu->drawing_area_x2 = 0;
	gpu->drawing_area_y2 = 0;
	gpu->drawing_offset_x = 0;
	gpu->drawing_offset_y = 0;
	gpu->display_area_src_x = 0;
	gpu->display_area_src_y = 0;
	gpu->display_area_dest_x1 = 0;
	gpu->display_area_dest_x2 = 0;
	gpu->display_area_dest_y1 = 0;
	gpu->display_area_dest_y2 = 0;

	/* Enable clock */
	gpu->clock.enabled = true;
}

void gpu_deinit(struct controller_instance *instance)
{
	free(instance->priv_data);
}

CONTROLLER_START(gpu)
	.init = gpu_init,
	.reset = gpu_reset,
	.deinit = gpu_deinit
CONTROLLER_END

