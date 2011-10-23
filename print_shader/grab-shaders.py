#!/usr/bin/python

import sys, os, os.path, glob

usage = """
 Function: collect shaders.

 Usage: 
  grab-shaders.py <shaders dir> <output file>
  shaders-dir is expected to contain file pairs
  of form <name>.fs and <name>.vs
  output-file contains generated C code
  resulting structure array is:
  
  struct _shader_pair {
    char *name;
    GLchar * v_src[1], * f_src[1];
    GLint v_len, f_len;
  } shader_collection [];
  
  first and last element of which is { NULL, NULL, NULL, 0, 0 };
  first element is reserved to unify with loading external shader code
  
  
"""

def main():
    sdir = sys.argv[1]
    fname = sys.argv[2]
    
    vsp = glob.glob(os.path.join(sdir, '*.vs'))
    fsp = glob.glob(os.path.join(sdir, '*.fs'))
    
    vstab = []
    for n in vsp:
        vstab.append(os.path.basename(n))
        
    vfstab = []
    for n in fsp:
        vsn = os.path.basename(n)[:-2] + 'vs'
        if vsn in vstab:
            vfstab.append((vsn, os.path.basename(n)))
    if len(vfstab) == 0:
        sys.stderr.write("No shaders found")
        raise SystemExit(1)
    os.chdir(sdir)
    
    c_struct = """
struct _shader_pair {
    const char *name;
    GLchar *v_src[1], *f_src[1];
    GLint v_len[1], f_len[1];
} shader_collection[] = {
    {
        "", 
        { NULL },
        { NULL },
        { NULL },
        { NULL }
    },
"""
    item_f = """    {{ 
        "{0}", 
        {{ {1} }},
        {{ {2} }},
        {{ sizeof({1})-1 }},
        {{ sizeof({2})-1 }}
    }}"""
    c_chars = []
    def xpmize(fname, name):
        rv = 'GLchar {0}[] = {{ \n    '.format(name)
        zeppelin = 0
        for c in file(fname).read():
            zeppelin += 1
            if zeppelin % 16 == 0:
                rv += "\n    "
            elif zeppelin % 8 == 0:
                rv += ' '
            rv += "0x{0:02x},".format(ord(c))
        return rv + " 0x00\n};";
    c_items = []
    for t in vfstab:
        vs, fs = t
        vs_sym = vs.replace('.', '_')
        fs_sym = fs.replace('.', '_')
        c_chars.append(xpmize(vs, vs_sym))
        c_chars.append(xpmize(fs, fs_sym))
        c_items.append(item_f.format(vs[:-3], vs_sym, fs_sym))

    c_code = "\n".join(c_chars) 
    c_code += c_struct +  ",\n".join(c_items) + "\n};\n"
    
    file(fname, "w").write(c_code)

if __name__ == "__main__": 
    main()
    
    




