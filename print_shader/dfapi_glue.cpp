/**
dfapi glue
*/
#include <map>
#include "df/api.h"

class pms_esse : public df::api::essentials {
    std::map<std::string, uint32_t> symbols;
    std::map<void *, std::string> classnames;
 	public:
        
    pms_esse() { //v0.31.25linux hardcoded just for now.
        symbols[std::string("cursor")] = 0x8c3de60;
        symbols[std::string("selection_rect")] = 0x8c3de70;
        symbols[std::string("control_mode")] = 0x8c3de90;
        symbols[std::string("game_mode")] = 0x8c3dea0;
        symbols[std::string("window_x")] = 0x8dfaa98;
        symbols[std::string("window_y")] = 0x8dfaa9c;
        symbols[std::string("window_z")] = 0x8dfaaa0;
        symbols[std::string("world")] = 0x93f77a0;
        symbols[std::string("pause_state")] = 0x93f06f0;
        symbols[std::string("ui")] = 0x93f0780;
        symbols[std::string("gview")] = 0x8c3e900;
        symbols[std::string("init")] = 0x959c2a0;
        symbols[std::string("d_init")] = 0x959d340;

        symbols[std::string("ui_sidebar_menus")] = 0x958aa40;
        symbols[std::string("ui_build_selector")] = 0x8df9b20;
        symbols[std::string("ui_look_list")] = 0x961d840;
        symbols[std::string("ui_look_cursor")] = 0x93f06dc;
        symbols[std::string("ui_workshop_job_cursor")] = 0x93f0644;
        symbols[std::string("ui_workshop_in_add")] = 0x93f0651;

        symbols[std::string("job_next_id")] = 0x961d940;

        symbols[std::string("ui_building_item_cursor")] = 0x93f0648;
        symbols[std::string("ui_selected_unit")] = 0x93f06d0;
        symbols[std::string("ui_unit_view_mode")] = 0x93f06d8;
        symbols[std::string("current_weather")]= 0x93f05e4;
        symbols[std::string("cur_year")] = 0x93f0600;
        symbols[std::string("cur_year_tick")] = 0x93f0620;

        symbols[std::string("gps")] = 0x8c3e000;        
        
    }

    std::string readClassName(void *vptr) {
        std::map<void *, std::string>::iterator it = classnames.find(vptr);
        if (it != classnames.end())
            return it->second;

        //char * typeinfo = * (char **) ( (char *)vptr - 0x4) ;
        char * typestring = * (char **) ( (char *)vptr + 0x4) ;
        std::string raw = std::string((char *)typestring);
        size_t start = raw.find_first_of("abcdefghijklmnopqrstuvwxyz");// trim numbers
        size_t end = raw.length();
        return classnames[vptr] = raw.substr(start,end-start);
    }

    void lock()  { }    // no locking because we're the only thread 
    void unlock() { }   // and do any peeking only when simulation is paused.
    void *getGlobal(const char *name) {
        auto i = symbols.find(name);
        if(i == symbols.end())
            return 0;
        return (void *)( (*i).second );
    }
};

static pms_esse pmesse;


void dfapi_init(void) {
    df::global::InitGlobals(&pmesse);
}





