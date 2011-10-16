
import sys, time, math, struct, io, ctypes
from random import random as rnd
import pygame
	
from OpenGL.GL import *
from OpenGL.arrays import vbo
from OpenGL.GL.shaders import *
from OpenGL.GLU import *

import numpy as np

def loadtex(fname):
    glMatrixMode(GL_TEXTURE)
    glLoadIdentity()
    glMatrixMode(GL_MODELVIEW)
    
    texture_id = glGenTextures(1)
    surface = pygame.image.load(fname)
    surface.convert_alpha()
    stuff = pygame.image.tostring(surface, "RGBA")
    glBindTexture(GL_TEXTURE_2D, texture_id)
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, surface.get_width(), surface.get_height(), 
        0, GL_RGBA, GL_UNSIGNED_BYTE, stuff )
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
    return (texture_id, surface.get_width(), surface.get_height() )
    
def rgba32f():
    tid = glGenTextures(1)
    glBindTexture(GL_TEXTURE_2D, tid)
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, surface.get_width(), surface.get_height(), 
        0, GL_RGBA, GL_UNSIGNED_BYTE, stuff )
    print glGetInteger(GL_MAX_TEXTURE_UNITS)
    print glGetInteger(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS)

VERTEX_SHADERZOR = file("vertex.shader").read()
FRAGMENT_SHADERZOR = file("fragment.shader").read()

class Frame(dict):
    pass
class FindexError(Exception):
    pass
class FrameLoader(object):
    def __init__(self, fname):
        self.frames = []
        self.fd = file(fname, 'rb' )
        self.framecount = None
        self._sizeof_tile = 4+4+1+1+1+1 
        
    def frame(self, frame_i):
        if self.framecount and frame_i > self.framecount - 1:
            raise FindexError
        if (len(self.frames) <= frame_i) or (self.frames[frame_i] is None):
            self._load_frame(frame_i)
        return self.frames[frame_i]
            
    def _load_frame(self, frame_i):
        """ loads frames up to frame_i """
        if (len(self.frames) <= frame_i):
            for i in xrange(frame_i - len(self.frames) + 1):
                self.frames.append(None)
        posn = 0
        for idx in xrange(len(self.frames)):
            frame = self.frames[idx]
            if frame is None:         
                self.fd.seek(posn)
                frame = Frame()
                fheader = self.fd.read(8)
                if len(fheader) < 8: # EOF
                    self.framecount = idx
                    raise FindexError
                    
                frame.wt,frame.ht,frame.wpx,frame.hpx = struct.unpack("iiii", fheader)
                tilecount = frame.wt * frame.ht
                frame.update({
                    "screen":           ( np.fromfile(self.fd, np.uint8,  tilecount * 4), 4, GL_UNSIGNED_BYTE, 0),
                    "texpos":           ( np.fromfile(self.fd, np.uint32, tilecount), 1, GL_UNSIGNED_INT, 0),
                    "addcolor":         ( np.fromfile(self.fd, np.uint8,  tilecount), 1, GL_UNSIGNED_BYTE, 0),
                    "grayscale":        ( np.fromfile(self.fd, np.uint8,  tilecount), 1, GL_UNSIGNED_BYTE, 0),
                    "cf":               ( np.fromfile(self.fd, np.uint8,  tilecount), 1, GL_UNSIGNED_BYTE, 0),
                    "cbr":              ( np.fromfile(self.fd, np.uint8,  tilecount), 1, GL_UNSIGNED_BYTE, 0),
                    "screen_underlay":  ( np.fromfile(self.fd, np.uint32,  tilecount), 1, GL_UNSIGNED_INT, 0),
                })
                self.frames[idx] = frame
                print "frame {0}, tile(0,0) = {1} fg={2} bg={3} bold={4}".format(idx, 
                    frame["screen"][0][0], 
                    frame["screen"][0][1], 
                    frame["screen"][0][2], 
                    frame["screen"][0][3] )

            else:
                tilecount = frame.w * frame.h
            
            posn += 8 + tilecount * self._sizeof_tile
            idx += 1

        
class rednener(object):
    def __init__(self, fonttex, loader):
        self.snap_to_grid = False
        
        texdump, fcell_w, fcell_h = fonttex
        frame0 = loader.frame(0)

        self.w_px = frame0.w * fcell_w
        self.h_px = frame0.h * fcell_h
        
        if fcell_h > fcell_w:
            self.psize = fcell_h
            self.parx = (1.0*fcell_h)/fcell_w
            self.pary = 1.0
        else:
            self.psize = fcell_w
            self.parx = 1.0
            self.pary = (1.0*fcell_w)/fcell_h
        
        self.loader = loader
        self.initializeDisplay()
        self.font_txid, ftex_w, ftex_h = loadtex(texdump)
        print "font texture: {0}x{1}".format(ftex_w, ftex_h)
        class txszst(object):
            def __str__(self):
                return "{0}x{1}t".format(self.wt, self.ht)
        self.txsz = txszst()
        self.txsz.wt = ftex_w/fcell_w
        self.txsz.ht = ftex_h/fcell_h
        
        self.x = 0
        self.y = 0
        
        self.shader_setup()
        self.reset_vbos = True
        

    def initializeDisplay(self):
        self.reset_videomode()
        
        glEnable(GL_ALPHA_TEST)
        glAlphaFunc(GL_NOTEQUAL, 0)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glDisable(GL_DEPTH_TEST)
        glDepthMask(GL_FALSE)
        glEnable(GL_POINT_SPRITE_ARB)
        
    def shader_setup(self):
        VERTEX_SHADER = compileShader(VERTEX_SHADERZOR, GL_VERTEX_SHADER) 
        FRAGMENT_SHADER = compileShader(FRAGMENT_SHADERZOR, GL_FRAGMENT_SHADER)
        self.shader = compileProgram(VERTEX_SHADER, FRAGMENT_SHADER)
        glUseProgram(self.shader)
        
        uniforms = "font ansi txsz final_alpha pointsize viewpoint par".split()
        attributes = "screen texpos addcolor grayscale cf cbr position".split()

        self.uloc = {}
        for u in uniforms:
            self.uloc[u] = glGetUniformLocation(self.shader, u)
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
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_1D, self.ansi_txid)
        glActiveTexture(GL_TEXTURE1)
        glBindTexture(GL_TEXTURE_2D, self.font_txid)
        glUniform1i(self.uloc["ansi"], 0) # GL_TEXTURE0 : ansi color strip
        glUniform1i(self.uloc["font"], 1) # GL_TEXTURE1 : font
        glUniform2f(self.uloc["txsz"], self.txsz.wt, self.txsz.ht)  # tex size in tiles; 
        print "txsz: {0}".format(self.txsz)
        glUniform2f(self.uloc["viewpoint"], 0, 0)
        glUniform1f(self.uloc["final_alpha"], 0.75)
        glUniform1f(self.uloc["pointsize"], self.psize)
        
    def update_vbos(self, frame):            
        if self.reset_vbos:
            self.vbo = {}
            for a in frame.keys():
                ary, numelts, eltype, stride = frame[a]
                self.vbo[a] = vbo.VBO(ary, usage=GL_DYNAMIC_DRAW)
                self.vbo[a].bind()
                glEnableVertexAttribArray(self.aloc[a])
                glVertexAttribPointer(self.aloc[a], numelts, eltype, False, stride, self.vbo[a])
                print "bound vbo {2} to self.aloc[{1}] = {0} size {3}".format(self.aloc[a], a, self.vbo[a], self.vbo[a].size)
            self.makegrid(frame.w, frame.h)
            self.reset_vbos = False
        else:
            for a in frame.keys():
                ary,u,u,u = frame[a]
                self.vbo[a].set_array(ary)
                self.vbo[a].bind()
                
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
        self.grid_vbo.unbind()

    def makeansitex(self):
        """ makes a 16x1 1D texture with 16 ANSI colors """
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
        glBindTexture(GL_TEXTURE_1D, self.ansi_txid)
        glTexImage1D( GL_TEXTURE_1D, 0, GL_RGBA, len(ansi_list), 0, GL_RGBA, GL_UNSIGNED_BYTE, ansi_stuff)
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP)
        #glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)
        glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
        glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST)
        
    def render_frame(self, bgc, alpha):                
        glUniform1f(self.uloc["final_alpha"], alpha)
        glClearColor(*bgc)
        glClear(GL_COLOR_BUFFER_BIT)
        glDrawArrays(GL_POINTS, 0, self.vertexcount)
        pygame.display.flip()
        
    def loop(self, fps, start_frame):
        bgc = ( 0.0, 0.3, 0.0 ,1 )
        slt = 1.0/fps
        gslt = 1.0/30
        last_frame_ts = 0
        frame_i = start_frame
        paused = False
        finished = False
        panning = False
        lastframesize = ( 0, 0 )
        next_frame = False
        while not finished:
            last_render_ts = time.time()
            
            if not paused and last_render_ts - last_frame_ts > slt:
                last_frame_ts = time.time()
                frame_i += 1
                try:
                    frame = self.loader.frame(frame_i)
                except FindexError:
                    frame_i = start_frame
                    frame = self.loader.frame(frame_i)
                next_frame = True
                
            if next_frame:
                if paused:
                    pause_str = ", pause.";
                else:
                    pause_str = ""
                #print "Frame {0}{1}".format(frame_i, pause_str)

                if (frame.w, frame.h) != lastframesize:
                    print "framesize check: ({0}, {1}) != {2}".format(frame.w, frame.h, lastframesize)
                    lastframesize =  (frame.w, frame.h)
                    self.reset_vbo = True

            for ev in pygame.event.get():
                if ev.type == pygame.KEYDOWN:
                    if ev.key == pygame.K_SPACE:
                        paused = not paused
                    elif ev.key == pygame.K_ESCAPE:
                        finished = True 
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
                        
            self.update_vbos(frame)
            self.render_frame(bgc, 1.0)

            render_time = time.time() - last_render_ts
            _gslt = gslt - render_time
            if _gslt > 0:
                time.sleep(_gslt)
            else:
                print "drawing's too slow, {0:.2f} FPS vs {1:.2f} requested\n".format(1.0/render_time, fps)

    def fini(self):
        self.shader = None
        self.vbo = None
        glDeleteTextures((self.ansi_txid, self.font_txid))
        pygame.quit()


if __name__ == "__main__":
    tex = ("texdump0002.png", 16, 16)
    loader = FrameLoader(sys.argv[1])
    r = rednener(tex, loader)
    r.loop(fps=float(sys.argv[2]), start_frame=int(sys.argv[3]))
    r.fini()