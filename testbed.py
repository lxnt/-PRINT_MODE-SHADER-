#!/usr/bin/python
# -*- encoding: utf-8 -*-

import sys, time, math, struct, io, ctypes, zlib, collections, argparse, traceback, os, types
from random import random as rnd
import pygame
	
from OpenGL.GL import *
from OpenGL.arrays import vbo
from OpenGL.GL.shaders import *
from OpenGL.GLU import *

import numpy as np

def rgba32f():
    tid = glGenTextures(1)
    glBindTexture(GL_TEXTURE_2D, tid)
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, surface.get_width(), surface.get_height(), 
        0, GL_RGBA, GL_UNSIGNED_BYTE, stuff )
    print glGetInteger(GL_MAX_TEXTURE_UNITS)
    print glGetInteger(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS)

class EndOfFile(Exception):
    pass

class Frame(object):
    class _bo_offset(object):
        def __init__(self, args):
            self.screen = args[0]
            self.underlay = args[1]
            self.texpos = args[2]
            self.addcolor = args[3]
            self.grayscale = args[4]
            self.cf = args[5]
            self.cbr = args[6]
            
    s_hdr = struct.Struct(
            "7i" #  _bo_offset
          +  "i" #  data_len
          + "2i" #  grid w/h
          + "3i" #  Pszx, Pszy, Psz
          + "2i" #  viewport_offset_x, viewport_offset_y
          + "2i" #  viewport_w, viewport_h
          + "2i" #  surface_w, surface_h
          + "2i" #  txsz_w, txsz_h
          + "2i" #  tile_w, tile_h
          +  "i" #  frame_number
          +  "i" #  texture_generation
    )
    def __init__(self, fd):
        b = fd.read(self.s_hdr.size)
        if len(b) != self.s_hdr.size:
            raise EndOfFile
        d = list(self.s_hdr.unpack(b))
        self.bo = self._bo_offset(d[:7])
        self.data_len = d[7]
        self.grid_w, self.grid_h = d[8:10]
        self.Pszx, self.Pszy, self.Psz = d[10:13]
        self.viewport_offset_x, self.viewport_offset_y = d[13:15]
        self.viewport_w, self.viewport_h = d[15:17]
        self.surface_w, self.surface_h = d[17:19]
        self.txsz_w, self.txsz_h = d[19:21]
        self.tile_w, self.tile_h = d[21:23]
        self.frame_number = d[23]
        self.texture_generation = d[24]
        self.data_offs = fd.tell()
        fd.seek(self.data_len, os.SEEK_CUR)
        #print "frame {0} cdatalen {1}".format(self.frame_number, self.data_len)
        self.data = None
        self.fd = fd
        self._buffer = None
    def dump(self):
        if not self.data:
            self._read_data()
        file("screen", "w").write(buffer(self.data, self.bo.screen, self.bo.underlay - self.bo.screen))
        file("texpos", "w").write(buffer(self.data, self.bo.texpos, self.bo.addcolor - self.bo.texpos))
    def _read_data(self):
        self.fd.seek(self.data_offs)
        d = self.fd.read(self.data_len)
        if len(d) != self.data_len:
            raise EndOfFile("frame={0} data_offs={1} data_len={2} tell={3} read={4}".format(
                self.frame_number, self.data_offs, self.data_len, self.fd.tell(), len(d)))
        self.data = zlib.decompress(d)
    
    def buf(self):
#        if not self._buffer:
#            if not self.data:
#                self._read_data()
#            self._buffer = buffer(self.data)
        if not self.data:
            self._read_data()
        return self.data
    
    def nparrays(self):
        if not self.data:
            self._read_data()        
        tilecount = self.grid_h * self.grid_w
        class _npa_bunch(object):
            pass
        rv = _npa_bunch()
        rv.screen    = np.fromfile(buffer(self.data, self.bo.screen),    np.uint8,  tilecount * 4)
        rv.underlay  = np.fromfile(buffer(self.data, self.bo.underlay),  np.uint8,  tilecount * 4)
        rv.texpos    = np.fromfile(buffer(self.data, self.bo.texpos),    np.uint32, tilecount    )
        rv.addcolor  = np.fromfile(buffer(self.data, self.bo.addcolor),  np.uint8,  tilecount    )
        rv.grayscale = np.fromfile(buffer(self.data, self.bo.grayscale), np.uint8,  tilecount    )
        rv.cf        = np.fromfile(buffer(self.data, self.bo.cf),        np.uint8,  tilecount    )
        rv.cbr       = np.fromfile(buffer(self.data, self.bo.cbr),       np.uint8,  tilecount    )
        return rv

class StuffDump(collections.Sequence):
    t_hdr = struct.Struct(
            "2i" # w_t, h_t - tex grid size
          + "2i" # t_w, t_h - tex cell size
    )
    
    def __init__(self, dname, filter=GL_NEAREST):
        self.dname = dname
        self.frames = []
        self.textures = []
        self.fd = file(dname + ".sdump", 'rb' )
        self.framecount = None
        self.filter = filter 
        self._build_index()

        print "FrameLoader(): {0} frames indexed\n".format(len(self.frames))

    def _build_index(self):
        while True:
            try:
                f = Frame(self.fd)
                last_tex_gen = f.texture_generation
                self.frames.append(f)
            except EndOfFile:
                break
        self._load_textures(self.dname, last_tex_gen, self.filter)
    
    def __len__(self):
        return len(self.frames)

    def __getitem__(self, frame_i):
        return (self.frames[frame_i], self.textures[self.frames[frame_i].texture_generation])

    def _load_textures(self, fprefix, last_gen, filter=GL_LINEAR):
        for idx in xrange(last_gen + 1):
            fname = "{0}{1:04d}".format(fprefix, last_gen)
            
            szdata = file(fname + ".tsz")
            w_t, h_t, t_w, t_h = self.t_hdr.unpack(szdata.read(self.t_hdr.size))
            sizes = szdata.read()
            assert len(sizes) == 4*w_t*h_t
            
            surface = pygame.image.load(fname + ".png")
            #surface.convert_alpha()
            assert w_t*t_w == surface.get_width()
            assert h_t*t_h == surface.get_height()
            stuff = pygame.image.tostring(surface, "RGBA")            
            self.textures.append((stuff, w_t, h_t, t_w, t_h, sizes, idx))
            print "texture {0} data loaded".format(idx)

class rednener(object):
    def __init__(self, loader, vs, fs, textarget):
        self.snap_to_grid = False
        self.textarget = textarget
        
        frame0, tex0 = loader[0]

        self.w_px = frame0.surface_w
        self.h_px = frame0.surface_h
        
        self.loader = loader
        self.initializeDisplay()
        self.vs = vs
        self.fs = fs
        self.shader_setup()
        self.reset_vbos = True
        
        self.texgen = None
        self.filter = GL_NEAREST
        
        
    def initializeDisplay(self):
        self.reset_videomode()
        self.glinfo()
        glEnable(GL_ALPHA_TEST)
        glAlphaFunc(GL_NOTEQUAL, 0)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glDisable(GL_DEPTH_TEST)
        glDepthMask(GL_FALSE)
        glEnable(GL_POINT_SPRITE)
        glEnable(GL_PROGRAM_POINT_SIZE)
        #glDisable(GL_POINT_SMOOTH)
        glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_UPPER_LEFT)
        self.font_txid, self.txco_txid = glGenTextures(2)
        
    def glinfo(self):
        strs = {
            GL_VENDOR: "vendor",
            GL_RENDERER: "renderer",
            GL_VERSION: "version",
            GL_SHADING_LANGUAGE_VERSION: "GLSL version",
        }
        ints = [
            (    7, GL_MAX_VERTEX_ATTRIBS, "GL_MAX_VERTEX_ATTRIBS" ), # number of vec4 attribs available
            (    9, GL_MAX_VERTEX_UNIFORM_COMPONENTS, "GL_MAX_VERTEX_UNIFORM_COMPONENTS" ), # single-component values
            (    8, GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS" ), # same as above
            (    1, GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS" ), # samplers in vert shader
            (    2, GL_MAX_TEXTURE_IMAGE_UNITS, "GL_MAX_TEXTURE_IMAGE_UNITS" ),  # samplers in frag shader
            (    3, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS" ), # samplers in vert shader
            (   12, GL_MAX_VARYING_FLOATS, "GL_MAX_VARYING_FLOATS" ), # 4 varying_floats = 1 texture_coord?
            (    3, GL_MAX_TEXTURE_COORDS, "GL_MAX_TEXTURE_COORDS" ), # 1 texture_coord = 4 varying_floats?
            (   -4, GL_POINT_SIZE_MIN, "GL_POINT_SIZE_MIN" ),
            (   32, GL_POINT_SIZE_MAX, "GL_POINT_SIZE_MAX" ), # no idea of our requirements
            ( 2048, GL_MAX_RECTANGLE_TEXTURE_SIZE, "GL_MAX_RECTANGLE_TEXTURE_SIZE" ),
        ]
        exts = glGetString(GL_EXTENSIONS)
        for e,s in strs.items():
            print "{0}: {1}".format(s, glGetString(e))
        for t in ints:
            p = glGetInteger(t[1])
            if (p<t[0]) or ((t[0]<0) and (p+t[0] >0)):
                w = "** "
            else:
                w = ""
            print "{3}{0}: {1} needed:{2}".format(t[2], p, abs(t[0]), w)
        if  "GL_ARB_texture_rectangle" in exts:
            print "GL_ARB_texture_rectangle: supported"
        else:
            print "GL_ARB_texture_rectangle: NOT SUPPORTED"
        
    def shader_setup(self, nominal=True):
        print "Compiling shaders: \n {0}\n {1}".format(self.vs, self.fs)
        vs = file(self.vs).readlines()
        fs = file(self.fs).readlines()
        if nominal:
            vs.insert(5,"#define NOMINAL")
            fs.insert(5,"#define NOMINAL")
        self.shader = compileProgram(
            compileShader("\n".join(vs), GL_VERTEX_SHADER), 
            compileShader("\n".join(fs), GL_FRAGMENT_SHADER))
        glUseProgram(self.shader)
        
        uniforms = "font ansi txco txsz final_alpha viewpoint pszar".split()
        attributes = "screen texpos addcolor grayscale cf cbr position".split()

        self.uloc = {}
        for u in uniforms:
            self.uloc[u] = glGetUniformLocation(self.shader, u)
            print "{0}: {1:08x}".format(u, self.uloc[u])
        self.aloc = {}
        for a in attributes:
            self.aloc[a] = glGetAttribLocation(self.shader, a)
            try:
                glEnableVertexAttribArray(self.aloc[a])
            except Exception, e:
                print "failed enabling VAA for {0}".format(a)
                print e
                raise
            
        self.makeansitex()
        print repr(self.uloc)
        glUniform1i(self.uloc["ansi"], 1) # GL_TEXTURE0 : ansi color strip
        glUniform1i(self.uloc["font"], 2) # GL_TEXTURE1 : font
        glUniform1i(self.uloc["txco"], 3) # GL_TEXTURE1 : txco
        glUniform1f(self.uloc["final_alpha"], 1.0)
        glUniform2f(self.uloc["viewpoint"], 0, 0);

    def reload_shaders(self, frame, texture, nominal):
        glDeleteProgram(self.shader)
        print "reload_shaders(): shaders dropped, reloading with nominal={0}".format(nominal)
        self.shader_setup(nominal)
        w_t, h_t, t_w, t_h = texture[1:5]
        glUniform4f(self.uloc["txsz"], w_t, h_t, t_w, t_h )  # tex size in tiles, tile size in texels
        self.reset_vbos = True

    def upload_textures(self, texture): 
        stuff, w_t, h_t, t_w, t_h, sizes, gen = texture
        glMatrixMode(GL_TEXTURE)
        glLoadIdentity()
        glMatrixMode(GL_MODELVIEW)
        glActiveTexture(GL_TEXTURE2)
        glBindTexture(self.textarget,  self.font_txid)
        #clamp = GL_CLAMP_TO_EDGE
        clamp = GL_REPEAT
        glTexParameteri(self.textarget, GL_TEXTURE_WRAP_S, clamp)
        glTexParameteri(self.textarget, GL_TEXTURE_WRAP_T, clamp)
        glTexParameterf(self.textarget, GL_TEXTURE_MAG_FILTER, self.filter)
        glTexParameterf(self.textarget, GL_TEXTURE_MIN_FILTER, self.filter)
        glTexImage2D(self.textarget, 0, GL_RGBA8, w_t*t_w, h_t*t_h, 
            0, GL_RGBA, GL_UNSIGNED_BYTE, stuff )
        
        glActiveTexture(GL_TEXTURE4)
        glBindTexture(self.textarget, self.txco_txid)

        glTexParameteri(self.textarget, GL_TEXTURE_WRAP_S, clamp)
        glTexParameteri(self.textarget, GL_TEXTURE_WRAP_T, clamp)
        glTexParameterf(self.textarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
        glTexParameterf(self.textarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST)        
        glTexImage2D(self.textarget, 0, GL_RGBA8, w_t, h_t, 
            0, GL_RGBA, GL_UNSIGNED_BYTE, sizes )
            
        self.txsz = ( w_t, h_t, t_w, t_h )

        glUniform4f(self.uloc["txsz"],*self.txsz )  # tex size in tiles, tile size in texels
        print "txsz set to ( {0:0.2f}, {1:0.2f}, {2:0.2f}, {3:0.2f} )".format( w_t, h_t, t_w, t_h )
        

    def rebind_textures(self):
        glActiveTexture(GL_TEXTURE4)
        glBindTexture(self.textarget, self.txco_txid)        
        glUniform1i(self.uloc["txco"], 4) # GL_TEXTURE1 : font
        glActiveTexture(GL_TEXTURE2)
        glBindTexture(self.textarget,  self.font_txid)
        glUniform1i(self.uloc["font"], 2) # GL_TEXTURE1 : font
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(self.textarget,  self.ansi_txid)
        glUniform1i(self.uloc["ansi"], 0) # GL_TEXTURE0 : ansi color strip
        
    def update_all_uniforms(self):
        #glUniform4f(self.uloc["txsz"], w_t, h_t, t_w, t_h )  # tex size in tiles, tile size in texels
        glUniform1f(self.uloc["final_alpha"], 1.0)
        glUniform2f(self.uloc["viewpoint"], 0, 0)
        glUniform4f(self.uloc["txsz"],*self.txsz ) 
        
    def update_vbos(self, frame, texture):
        if self.texgen != frame.texture_generation:
            self.texgen = frame.texture_generation
            self.upload_textures(texture)

        Parx = 1.0;
        Pary = 1.0;
        if frame.Pszx > frame.Pszy:
            Pary = float(frame.Pszy)/float(frame.Pszx)
        else:
            Parx = float(frame.Pszx)/float(frame.Pszy)
        glUniform3f(self.uloc["pszar"], Parx, Pary, frame.Psz)

        if self.reset_vbos:
            #frame.dump()
            #raise SystemExit
            
            Parx = 1.0;
            Pary = 1.0;
            if frame.Pszx > frame.Pszy:
                Pary = float(frame.Pszy)/float(frame.Pszx)
            else:
                Parx = float(frame.Pszx)/float(frame.Pszy)
            glUniform3f(self.uloc["pszar"], Parx, Pary, frame.Psz)
                
            buf = frame.buf()
            self.screen_vbo = vbo.VBO(buf, usage=GL_STREAM_DRAW)
            self.screen_vbo.bind()
            
            glVertexAttribPointer(self.aloc["screen"], 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, self.screen_vbo + frame.bo.screen)
            #glVertexAttribPointer(self.aloc["screen"], 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, self.screen_vbo + frame.bo.underlay)
            glVertexAttribPointer(self.aloc["texpos"], 1 , GL_UNSIGNED_INT, GL_FALSE, 0, self.screen_vbo + frame.bo.texpos)
            glVertexAttribPointer(self.aloc["addcolor"], 1 , GL_UNSIGNED_BYTE, GL_FALSE, 0, self.screen_vbo + frame.bo.addcolor)
            glVertexAttribPointer(self.aloc["grayscale"], 1,  GL_UNSIGNED_BYTE, GL_FALSE, 0, self.screen_vbo + frame.bo.grayscale)
            glVertexAttribPointer(self.aloc["cf"], 1 , GL_UNSIGNED_BYTE, GL_FALSE, 0, self.screen_vbo + frame.bo.cf)
            glVertexAttribPointer(self.aloc["cbr"], 1 , GL_UNSIGNED_BYTE, GL_FALSE, 0, self.screen_vbo + frame.bo.cbr)
            self.makegrid(frame.grid_w, frame.grid_h)
            self.reset_vbos = False
        else:
            self.screen_vbo.set_array(frame.buf())
            self.screen_vbo.bind()

    def reset_videomode(self):
        pygame.display.set_mode((self.w_px,self.h_px), pygame.OPENGL|pygame.DOUBLEBUF|pygame.RESIZABLE)
        
        glMatrixMode(GL_MODELVIEW) # always so as we don't do any model->world->eye transform
        glLoadIdentity()

        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        
        gluOrtho2D(0, self.w_px, 0, self.h_px)
        #print repr(glGetFloatv(GL_PROJECTION_MATRIX))
        #glViewport(0, self.w_px, 0, self.h_px)
        glViewport(0, 0, self.w_px, self.h_px)

        glClearColor(0.3, 0.0, 0.0, 1.0)
        glClear(GL_COLOR_BUFFER_BIT)

    """ dis shit should work just like the game.
        that is, if grid size is not overriden,
        
        zoom: modify it, keeping in [(80,25), (256,256)]
                change psize as necessary
                
        reshape: modify it, keeping in [(80,25), (256,256)]
                change psize as necessary
    """
    MIN_GRID_X = 80
    MIN_GRID_Y = 25
    MAX_GRID_X = 256
    MAX_GRID_Y = 256
    
    def zoom(self, delta):
        psize  = self.psize + delta
        
        # get new grid size
        nw_t = math.floor(float(self.w_px)/psize)
        nh_t = math.floor(float(self.h_px)/psize)
        
        # clamp the crap, yarrrr
        nw_t = min(max(nw_t, 80), 256)
        nh_t = min(max(nh_t, 25), 256)
        
        # recalc resulting psize
        psize = math.floor(min(self.w_px/nw_t, self.h_px/nh_t))
        
        """ now what do we have:
            if the window is 
                so wide, that nw_t*(h_px/nh_t) > MAX_GRID_X
            or
                so tall, that n_ht*(w_px/nw_t) > MAX_GRID_Y
                
            the game just makes tiles change aspect ratio.
            if this is not an option, then prohibit zoom past that level
        """
        
        
        if self.snap_to_grid:
            self.w_px = self.psize*self.w_t
            self.h_px = self.psize*self.h_t
        
        #self.makegrid()
        
        
        self.reset_videomode()
        glUniform1f(self.uloc["pointsize"], psize)
    
    def reshape(self, frame): 
        """ now, we got what? wpx, hpx, w_t, h_t from da frame. That is all.
        
        """
        Pw = (1.0 * frame.wpx)/frame.w_t
        Ph = (1.0 * frame.hpx)/frame.h_t
        if Pw > Ph:
            Psize = Pw
            Parx = 1.0
            Pary = Ph/Pw
        else:            
            Psize = Ph
            Parx = Pw/Ph
            Pary = 1.0

        glUniform3f(self.uloc["pszar"], Parx, Pary, frame.Psz)
        
        self.w_px = frame.wpx
        self.h_px = frame.hpx
        
        self.makegrid(frame.w_t, frame.h_t)
        
    def makegrid(self, w, h):
        rv = []
        for xt in xrange(0, w):
            for yt in xrange(0, h):
                x = (xt + 0.5)#/w
                y = (h - yt - 0.5)#/h
                rv.append( [ x, y ] )
    
        self.vertexcount = len(rv)
        self.grid_vbo = vbo.VBO(np.array( rv, 'f' ), usage=GL_STATIC_DRAW)
        self.grid_vbo.bind()
        glVertexAttribPointer(self.aloc["position"], 2, GL_FLOAT, False, 0, self.grid_vbo)
        print "grid reset to {0} vertices".format(self.vertexcount)

    def makeansitex(self):
        """ makes a 16x1 2DRect texture with 16 ANSI colors """
        ansi_list = [
            ('BLACK', 0, 0, 0),
            ('BLUE', 46, 88, 255),
            ('GREEN', 70, 170, 56),
            ('CYAN', 56,136 , 170),
            ('RED',170 , 0, 0),
            ('MAGENTA',170 , 56, 136),
            ('BROWN', 170, 85, 28),
            ('LGRAY', 187, 177, 167),
            ('DGRAY', 135, 125, 115),
            ('LBLUE', 96, 128, 255),
            ('LGREEN', 105, 255, 84),
            ('LCYAN', 84, 212, 255),
            ('LRED', 255, 0, 0),
            ('LMAGENTA',255 , 84, 212),
            ('YELLOW', 255, 204, 0),
            ('WHITE', 255, 250, 245)
        ]
        ansi_stuff = ""
        for c in ansi_list:
            ansi_stuff += struct.pack("BBBB", c[1], c[2], c[3], 1)
        glMatrixMode(GL_TEXTURE)
        glLoadIdentity()
        glMatrixMode(GL_MODELVIEW)
        self.ansi_txid = glGenTextures(1)
        glActiveTexture(GL_TEXTURE0)        
        glBindTexture(self.textarget, self.ansi_txid)
        glTexImage2D( self.textarget, 0, GL_RGBA8, len(ansi_list), 1, 0, 
            GL_RGBA, GL_UNSIGNED_BYTE, ansi_stuff)
        glTexParameteri(self.textarget, GL_TEXTURE_WRAP_S, GL_CLAMP)
        glTexParameteri(self.textarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)
        glTexParameterf(self.textarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
        glTexParameterf(self.textarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST)
        print "makeansitex_rect {0}x{1}->{2}".format(len(ansi_list), 1,self.ansi_txid) 

    def render_frame(self, frame, texture, bgc, alpha):
        t = pygame.time.get_ticks()
        glUseProgram(self.shader)
        #glUniform1f(self.uloc["final_alpha"], alpha)
        self.update_vbos(frame, texture)
        glClearColor(*bgc)
        glClear(GL_COLOR_BUFFER_BIT)
        self.grid_vbo.bind()
        self.rebind_textures()
        self.update_all_uniforms()
        glUseProgram(self.shader)
        glDrawArrays(GL_POINTS, 0, self.vertexcount)
        pygame.display.flip()
        return  pygame.time.get_ticks() - t
        
    def loop(self, fps, gfps, start_frame):
        bgc = ( 0.0, 0.3, 0.0 ,1 )
        
        slt = 1000.0/fps # milliseconds
        gslt = 1000.0/gfps # milliseconds
        last_frame_ts = 0
        frame_i = start_frame
        paused = False
        finished = False
        panning = False
        lastframesize = ( 0, 0 )
        next_frame = False                      
        self.reset_vbo = True
        last_frame_ts = -1e23
        while not finished:
            last_render_ts = pygame.time.get_ticks()
            if not paused and (last_render_ts - last_frame_ts > slt):
                last_frame_ts = pygame.time.get_ticks()
                frame_i += 1
                try:
                    frame, texture = self.loader[frame_i]
                except IndexError:
                    frame_i = start_frame
                    frame, texture = self.loader[frame_i]
                next_frame = True
                
            if next_frame:
                if paused:
                    pause_str = ", pause.";
                else:
                    pause_str = ""

                if (frame.grid_w, frame.grid_h) != lastframesize:
                    print "framesize check: ({0}, {1}) != {2}".format(frame.grid_w, frame.grid_h, lastframesize)
                    lastframesize =  (frame.grid_w, frame.grid_h)
                    self.reset_vbo = True

            render_time = self.render_frame(frame, texture, bgc, 1.0)
            
            print "frame rendered in {0} msec".format(render_time)
            render_time += 1
            next_render_time = pygame.time.get_ticks() + gslt - render_time
            while  True:
                for ev in pygame.event.get():
                    if ev.type == pygame.KEYDOWN:
                        if ev.key == pygame.K_SPACE:
                            paused = not paused
                        elif ev.key == pygame.K_ESCAPE:
                            finished = True 
                        elif ev.key == pygame.K_F1:
                            self.reload_shaders(frame, texture, nominal=True)
                        elif ev.key == pygame.K_F2:
                            self.reload_shaders(frame, texture, nominal=False)
                    elif ev.type == pygame.QUIT:
                        finished = True
                    elif ev.type ==  pygame.VIDEORESIZE:
                        self.reshape(ev.w, ev.h)
                    elif ev.type == pygame.MOUSEBUTTONDOWN:
                        if ev.button == 4: # wheel forward
                            self.zoom(-1)
                        elif ev.button == 5: # wheel back
                            self.zoom(+1)
                        elif ev.button == 3: # RMB
                            panning = True
                        else:
                            paused = not paused
                    elif ev.type == pygame.MOUSEBUTTONUP:
                        if ev.button == 3:
                            #print "panned to {0}x{1}".format(self.x, self.y)
                            panning = False
                            glUniform2f(self.uloc["viewpoint"], self.x, self.y)
                    elif ev.type ==  pygame.MOUSEMOTION:
                        if panning:
                            self.x -= ev.rel[0]
                            self.y += ev.rel[1]
                            glUniform2f(self.uloc["viewpoint"], self.x, self.y)
                            
                if next_render_time - pygame.time.get_ticks() < -50:
                    print "drawing's too slow, {0:.2f} FPS vs {1:.2f} reqd".format(1000.0/render_time, gfps)
                    break
                elif next_render_time - pygame.time.get_ticks() < 0:
                    break
                elif next_render_time - pygame.time.get_ticks() < 50:
                    pygame.time.wait(int(next_render_time - pygame.time.get_ticks()))
                    break
                else:
                    pygame.time.wait(50)

    def fini(self):
        self.shader = None
        self.vbo = None
        glDeleteTextures((self.ansi_txid, self.font_txid))
        pygame.quit()


#~ debug_get_flags_option: help for ST_DEBUG:
#~ |      mesa [0x0000000000000001]
#~ |      tgsi [0x0000000000000002]
#~ | constants [0x0000000000000004]
#~ |      pipe [0x0000000000000008]
#~ |       tex [0x0000000000000010]
#~ |  fallback [0x0000000000000020]
#~ |    screen [0x0000000000000080]
#~ |     query [0x0000000000000040]

#~ debug_get_flags_option: help for LP_DEBUG: -- softpipe only
#~ |          pipe [0x0000000000000001]
#~ |          tgsi [0x0000000000000002]
#~ |           tex [0x0000000000000004]
#~ |         setup [0x0000000000000010]
#~ |          rast [0x0000000000000020]
#~ |         query [0x0000000000000040]
#~ |        screen [0x0000000000000080]
#~ |    show_tiles [0x0000000000000200]
#~ | show_subtiles [0x0000000000000400]
#~ |      counters [0x0000000000000800]
#~ |         scene [0x0000000000001000]
#~ |         fence [0x0000000000002000]
#~ |           mem [0x0000000000004000]
#~ |            fs [0x0000000000008000]

#~ debug_get_flags_option: help for GALLIVM_DEBUG:
#~ |         tgsi [0x0000000000000001]
#~ |           ir [0x0000000000000002]
#~ |          asm [0x0000000000000004]
#~ |         nopt [0x0000000000000008]
#~ |         perf [0x0000000000000010]
#~ | no_brilinear [0x0000000000000020]
#~ |           gc [0x0000000000000040]


#export LP_DEBUG=tgsi
#export ST_DEBUG=tgsi,mesa
#export GALLIVM_DEBUG=tgsi
#export MESA_DEBUG=y
#export LIBGL_DEBUG=verbose
#export GALLIUM_PRINT_OPTIONS=help
#export TGSI_PRINT_SANITY=y

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description = '[PRINT_MODE:SHADER] testbed')
    ap.add_argument('-fps', '--fps', metavar='fps', type=float, default=0.4)
    ap.add_argument('-rect', '--rect', metavar='texture mode', dest='tmode', action='store_const', const=GL_TEXTURE_RECTANGLE)
    ap.add_argument('-npot', '--npot', metavar='texture mode', dest='tmode', action='store_const', const=GL_TEXTURE_2D)
    ap.add_argument('-gfps', '--gfps', metavar='gfps', type=float, default=1.0)
    ap.add_argument('-s', '--start-frame', metavar='start frame', type=int, default=0)
    ap.add_argument('-vs', '--vertex-shader',  metavar='vertex shader', default='data/cbr_is_bold.vs')
    ap.add_argument('-fs', '--fragment-shader',  metavar='fragment shader', default='data/cbr_is_bold.fs')
    ap.add_argument('dumpname', metavar="dump_prefix", help="dump name prefix (foobar in foobar.sdump/foobar0000.png)")
    ap.add_argument('mesa', metavar="mesa_driver", nargs='?', default="hw", help="mesa driver, values: hw, hw-alt, sw, sw-alt")
        
    pa = ap.parse_args()
    if pa.tmode is None:
        pa.tmode = GL_TEXTURE_2D
        
    envi = {
        "hw": (),
        "hw-alt": (("LIBGL_DRIVERS_PATH","/usr/lib/x86_64-linux-gnu/dri-alternates"),),
        "sw" : (("LIBGL_ALWAYS_SOFTWARE","y"),),
        "sw-alt": (("LIBGL_ALWAYS_SOFTWARE","y"), ("LIBGL_DRIVERS_PATH","/usr/lib/x86_64-linux-gnu/dri-alternates"),)
    }
    
    for v in envi.values():
        for et in v:
            if et:
                try:
                    del os.environ[et[0]]
                except KeyError:
                    pass
    if pa.mesa:
        for et in envi[pa.mesa]:
            if et:
                os.environ[et[0]] = et[1]
    
    try:
        stuff = StuffDump(pa.dumpname)
        
        r = rednener(stuff, pa.vertex_shader, pa.fragment_shader, pa.tmode)
    except OSError, e:
        traceback.print_exc(e)
        ap.print_help()
        sys.exit(1)
    
    r.loop(fps=pa.fps, gfps=pa.gfps, start_frame=pa.start_frame)
    r.fini()
