#include "./earlylogo.h"
#include <generated/early-logo.h>
#include "linux/printk.h"
#include "linux/screen_info.h"
#include <linux/sysfb.h>
#include <linux/io.h>
#include <linux/screen_info.h>
#define private static
#define Fn(r, name, args...) r (*name)(args);
#define Renderer_self struct Renderer *self
#define Renderer_takeself struct Renderer self
struct Renderer {

	// Pointer to the start of the pixels, starting from the left top corner
	// The pixels read like a book from left to right, top to bottom.
	void* fb_pointer;
	Fn(void, free_fb, void __iomem*);



	// How many bits there are per pixel that are 
	// RGB888 would be 8 + 8 + 8 = 24 bits
	// A lot of times there's also padding to align it to 32 bits (XRGB) and make it x86 CPU friendly,
	// making it 32 bits instead
	u64 bits_per_pixel;

	// How many bits in each pixel are actually used for color.
	u64 depth;

	// Screen width in pixels
	u64 width;
	
	// Screen height in pixels
	u64 height;

	// Pitch of the screen, it's the amount of bytes a single row of pixels occupies
	u64 scanline_stride;
	
	struct {
		u8 red_pos;
		u8 green_pos;
		u8 blue_pos;
	} pixel_color_positions;
};
enum CreateRendererResultError {
	CreateRendererResultError__NoScreen,
	CreateRendererResultError__FailedToMMapFramebuffer
};
struct CreateRendererResult {
	bool ok;
	union {
		struct Renderer ok;
		enum CreateRendererResultError error;
	} inner;
};
private struct CreateRendererResult CreateRendererResult__Err(enum CreateRendererResultError error) {
	return (struct CreateRendererResult) {
		.ok = false,
		.inner = {
			.error = error
		}
	};
}
private struct CreateRendererResult Renderer__from_sysfb(struct sysfb_display_info* display_info) {
	if(!display_info) return CreateRendererResult__Err(CreateRendererResultError__NoScreen);
	struct screen_info* si = &display_info->screen;

	u64 base = __screen_info_lfb_base(si);
	if(!base) return CreateRendererResult__Err(CreateRendererResultError__NoScreen);
	u64 size = __screen_info_lfb_size(si, si->orig_video_isVGA);
	if(!size) return CreateRendererResult__Err(CreateRendererResultError__NoScreen);
	u32 stride  = __screen_info_lfb_bits_per_pixel(si);
	if(!stride) return CreateRendererResult__Err(CreateRendererResultError__NoScreen);
	void __iomem *fb = memremap(base, size, MEMREMAP_WC);
	if(!fb) return CreateRendererResult__Err(CreateRendererResultError__FailedToMMapFramebuffer);
	return (struct CreateRendererResult) {
		.ok = true,
		.inner = {
			.ok = {
				.fb_pointer = fb,
				.bits_per_pixel = stride,
				.depth = si->lfb_depth,
				.scanline_stride = si->lfb_linelength,
				.width = si->lfb_width,
				.height = si->lfb_height,
				.free_fb = memunmap,
				.pixel_color_positions = {
					.red_pos = si->red_pos,
					.green_pos = si->green_pos,
					.blue_pos = si->blue_pos,
				}
			}
		}
	};
}

private struct CreateRendererResult Renderer__from_primary_display(void) {
	return Renderer__from_sysfb(&sysfb_primary_display);
}

private inline u32 Renderer__pack_pixel(Renderer_self, u8 red, u8 green, u8 blue)
{
    return ((u32)red   << self->pixel_color_positions.red_pos)   |
           ((u32)green << self->pixel_color_positions.green_pos) |
           ((u32)blue  << self->pixel_color_positions.blue_pos);
}

private inline void* Renderer__pointer_to_pixel(Renderer_self, u32 x, u32 y) {
	u64 scanline_size = self->scanline_stride;
	u64 y_offset = scanline_size * y;
	u64 x_offset = (x*self->bits_per_pixel)/8;
	u64 pixel_offset = x_offset + y_offset;
	return (void*)(((u64)self->fb_pointer) + pixel_offset);
}

enum RendererSetPixelError {
	RendererSetPixelError__NoError,
	RendererSetPixelError__UnknownFormat
};

private inline enum RendererSetPixelError Renderer__set_pixel(Renderer_self, u32 color, u32 x, u32 y) {
	if(self->bits_per_pixel != 32) return RendererSetPixelError__UnknownFormat;
	if(self->depth != 32 && self->depth != 24) return RendererSetPixelError__UnknownFormat;
	volatile u32* pixel_pointer = Renderer__pointer_to_pixel(self, x, y);
	*pixel_pointer = color;
	return RendererSetPixelError__NoError;
}
/// Drops the renderer, taking ownership of self
private void Renderer__drop(Renderer_takeself) {
	self.free_fb(self.fb_pointer);
}

bool __init draw_early_boot_logo(void) {
	struct CreateRendererResult create_renderer_result = Renderer__from_primary_display();
	if(!create_renderer_result.ok) {
		switch(create_renderer_result.inner.error) {
			case CreateRendererResultError__NoScreen: {
				printk("Couldn't show early boot logo: No screen found");
				break;
			}
			case CreateRendererResultError__FailedToMMapFramebuffer: {
				printk("Couldn't show early boot logo: Failed to mmap the screen framebuffer");
				break;
			}
		}
		return false;
	}
	struct Renderer renderer = create_renderer_result.inner.ok;

	// Clear framebuffer to black
	memset_io(renderer.fb_pointer, 0, renderer.scanline_stride * renderer.height);

	// Center the logo on screen
	u32 offset_x = (renderer.width - early_logo_width) / 2;
	u32 offset_y = (renderer.height - early_logo_height) / 2;

	for (u32 y = 0; y < early_logo_height; y++) {
		for (u32 x = 0; x < early_logo_width; x++) {
			u32 i = (y * early_logo_width + x) * 3;
			u8 red   = early_logo_data[i + 0];
			u8 green = early_logo_data[i + 1];
			u8 blue  = early_logo_data[i + 2];
			u32 color = Renderer__pack_pixel(&renderer, red, green, blue);
			Renderer__set_pixel(&renderer, color, x + offset_x, y + offset_y);
		}
	}

	Renderer__drop(renderer);
	return true;
}
