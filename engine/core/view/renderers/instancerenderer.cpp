/***************************************************************************
 *   Copyright (C) 2005-2008 by the FIFE team                              *
 *   http://www.fifengine.de                                               *
 *   This file is part of FIFE.                                            *
 *                                                                         *
 *   FIFE is free software; you can redistribute it and/or modify          *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA              *
 ***************************************************************************/

// Standard C++ library includes

// 3rd party library includes

// FIFE includes
// These includes are split up in two parts, separated by one empty line
// First block: files included from the FIFE root src directory
// Second block: files included from the same folder
#include "video/renderbackend.h"
#include "video/image.h"
#include "video/sdl/sdlimage.h"
#include "video/imagepool.h"
#include "video/animation.h"
#include "video/animationpool.h"
#include "util/math/fife_math.h"
#include "util/log/logger.h"
#include "model/metamodel/grids/cellgrid.h"
#include "model/metamodel/action.h"
#include "model/structures/instance.h"
#include "model/structures/layer.h"
#include "model/structures/location.h"

#include "view/camera.h"
#include "view/visual.h"
#include "instancerenderer.h"


namespace FIFE {
	static Logger _log(LM_VIEWVIEW);

	InstanceRenderer::OutlineInfo::OutlineInfo(): 
		r(0), 
		g(0), 
		b(0), 
		width(1), 
		outline(NULL), 
		curimg(NULL) {
	}
	InstanceRenderer::ColoringInfo::ColoringInfo():
		r(0), 
		g(0), 
		b(0), 
		overlay(NULL),
		curimg(NULL) {
	}
	
	InstanceRenderer::OutlineInfo::~OutlineInfo() { 
		delete outline;
	}
	
	InstanceRenderer::ColoringInfo::~ColoringInfo() {
		delete overlay;
	}
	
	InstanceRenderer* InstanceRenderer::getInstance(IRendererContainer* cnt) {
		return dynamic_cast<InstanceRenderer*>(cnt->getRenderer("InstanceRenderer"));
	}
	
	InstanceRenderer::InstanceRenderer(RenderBackend* renderbackend, int position, ImagePool* imagepool, AnimationPool* animpool):
		RendererBase(renderbackend, position),
		m_imagepool(imagepool),
		m_animationpool(animpool),
		m_layer_to_outlinemap(),
		m_layer_to_coloringmap() {
		setEnabled(true);
	}

 	InstanceRenderer::InstanceRenderer(const InstanceRenderer& old):
		RendererBase(old),
		m_imagepool(old.m_imagepool),
		m_animationpool(old.m_animationpool),
		m_layer_to_outlinemap(),
		m_layer_to_coloringmap() {
		setEnabled(true);
	}

	RendererBase* InstanceRenderer::clone() {
		return new InstanceRenderer(*this);
	}

	InstanceRenderer::~InstanceRenderer() {
	}

	unsigned int scale(unsigned int val, double factor) {
		return static_cast<unsigned int>(ceil(static_cast<double>(val) * factor));
	}
	
	void InstanceRenderer::render(Camera* cam, Layer* layer, std::vector<Instance*>& instances) {
		FL_DBG(_log, "Iterating layer...");
		CellGrid* cg = layer->getCellGrid();
		if (!cg) {
			FL_WARN(_log, "No cellgrid assigned to layer, cannot draw instances");
			return;
		}

		bool potential_outlining = false;
		InstanceToOutlines_t outline_i2o;
		InstanceToOutlines_t::iterator outline_end;

		LayerToOutlineMap_t::iterator outline_l2i = m_layer_to_outlinemap.find(layer);
		if (outline_l2i != m_layer_to_outlinemap.end()) {
			potential_outlining = true;
			outline_i2o = outline_l2i->second;
			outline_end = outline_i2o.end();
		}

		bool potential_coloring = false;
		InstanceToColoring_t coloring_i2o;
		InstanceToColoring_t::iterator coloring_end;

		LayerToColoringMap_t::iterator coloring_l2i = m_layer_to_coloringmap.find(layer);
		if (coloring_l2i != m_layer_to_coloringmap.end()) {
			potential_coloring = true;
			coloring_i2o = coloring_l2i->second;
			coloring_end = coloring_i2o.end();
		}

		std::vector<Instance*>::const_iterator instance_it = instances.begin();
		for (;instance_it != instances.end(); ++instance_it) {
			FL_DBG(_log, "Iterating instances...");
			Instance* instance = (*instance_it);
			InstanceVisual* visual = instance->getVisual<InstanceVisual>();
			InstanceVisualCacheItem& vc = visual->getCacheItem(cam);
			FL_DBG(_log, LMsg("Instance layer coordinates = ") << instance->getLocationRef().getLayerCoordinates());
			
			if (potential_outlining) {
				InstanceToOutlines_t::iterator outline_it = outline_i2o.find(instance);
				if (outline_it != outline_end) {
					bindOutline(outline_it->second, vc, cam)->render(vc.dimensions);
				}
			}
			if (potential_coloring) {
				InstanceToColoring_t::iterator coloring_it = coloring_i2o.find(instance);
				if (coloring_it != coloring_end) {
					bindColoring(coloring_it->second, vc, cam)->render(vc.dimensions);
					continue;
				}
			}
			vc.image->render(vc.dimensions);
		}
	}
	
	Image* InstanceRenderer::bindOutline(OutlineInfo& info, InstanceVisualCacheItem& vc, Camera* cam) {
		if (info.curimg == vc.image) {
			return info.outline;
		}
		if (info.outline) {
			delete info.outline; // delete old mask
			info.outline = NULL;
		}
		SDL_Surface* surface = vc.image->getSurface();
		SDL_Surface* outline_surface = SDL_ConvertSurface(surface, surface->format, surface->flags);
		
		// needs to use SDLImage here, since GlImage does not support drawing primitives atm
		SDLImage* img = new SDLImage(outline_surface);
		
		// TODO: optimize...
		uint8_t r, g, b, a = 0;
		
		// vertical sweep
		for (unsigned int x = 0; x < img->getWidth(); x ++) {
			uint8_t prev_a = 0;
			for (unsigned int y = 0; y < img->getHeight(); y ++) {
				vc.image->getPixelRGBA(x, y, &r, &g, &b, &a);
				if ((a == 0 || prev_a == 0) && (a != prev_a)) {
					if (a < prev_a) {
						for (unsigned int yy = y; yy < y + info.width; yy++) {
							img->putPixel(x, yy, info.r, info.g, info.b);
						}
					} else {
						for (unsigned int yy = y - info.width; yy < y; yy++) {
							img->putPixel(x, yy, info.r, info.g, info.b);
						}
					}
				}
				prev_a = a;
			}
		}
		// horizontal sweep
		for (unsigned int y = 0; y < img->getHeight(); y ++) {
			uint8_t prev_a = 0;
			for (unsigned int x = 0; x < img->getWidth(); x ++) {
				vc.image->getPixelRGBA(x, y, &r, &g, &b, &a);
				if ((a == 0 || prev_a == 0) && (a != prev_a)) {
					if (a < prev_a) {
						for (unsigned int xx = x; xx < x + info.width; xx++) {
							img->putPixel(xx, y, info.r, info.g, info.b);
						}
					} else {
						for (unsigned int xx = x - info.width; xx < x; xx++) {
							img->putPixel(xx, y, info.r, info.g, info.b);
						}
					}
				}
				prev_a = a;
			}
		}
		
		// In case of OpenGL backend, SDLImage needs to be converted
		info.outline = m_renderbackend->createImage(img->detachSurface());
		delete img;
		return info.outline;
	}

	Image* InstanceRenderer::bindColoring(ColoringInfo& info, InstanceVisualCacheItem& vc, Camera* cam) {
		if (info.curimg == vc.image) {
			return info.overlay;
		}
		if (info.overlay) {
			delete info.overlay; // delete old mask
			info.overlay = NULL;
		}
		SDL_Surface* surface = vc.image->getSurface();
		SDL_Surface* overlay_surface = SDL_ConvertSurface(surface, surface->format, surface->flags);
		
		// needs to use SDLImage here, since GlImage does not support drawing primitives atm
		SDLImage* img = new SDLImage(overlay_surface);
		
		uint8_t r, g, b, a = 0;
		
		for (unsigned int x = 0; x < img->getWidth(); x ++) {
			for (unsigned int y = 0; y < img->getHeight(); y ++) {
				vc.image->getPixelRGBA(x, y, &r, &g, &b, &a);
				if (a > 0) {
					img->putPixel(x, y, (r + info.r) >> 1, (g + info.g) >> 1, (b + info.b) >> 1);
				}
			}
		}
		
		// In case of OpenGL backend, SDLImage needs to be converted
		info.overlay = m_renderbackend->createImage(img->detachSurface());
		delete img;
		return info.overlay;
	}

	void InstanceRenderer::addOutlined(Instance* instance, int r, int g, int b, int width) {
		OutlineInfo info;
		info.r = r;
		info.g = g;
		info.b = b;
		info.width = width;
		InstanceToOutlines_t& i2h = m_layer_to_outlinemap[instance->getLocation().getLayer()];
		i2h[instance] = info;
	}

	void InstanceRenderer::addColored(Instance* instance, int r, int g, int b) {
		ColoringInfo info;
		info.r = r;
		info.g = g;
		info.b = b;
		InstanceToColoring_t& i2h = m_layer_to_coloringmap[instance->getLocation().getLayer()];
		i2h[instance] = info;
	}

	void InstanceRenderer::removeOutlined(Instance* instance) {
		InstanceToOutlines_t& i2h = m_layer_to_outlinemap[instance->getLocation().getLayer()];
		i2h.erase(instance);
	}
	
	void InstanceRenderer::removeColored(Instance* instance) {
		InstanceToColoring_t& i2h = m_layer_to_coloringmap[instance->getLocation().getLayer()];
		i2h.erase(instance);
	}
	
	void InstanceRenderer::removeAllOutlines() {
		m_layer_to_outlinemap.clear();
	}
	
	void InstanceRenderer::removeAllColored() {
		m_layer_to_coloringmap.clear();
	}
	
	void InstanceRenderer::reset() {
		removeAllOutlines();
		removeAllColored();
	}
	
}
