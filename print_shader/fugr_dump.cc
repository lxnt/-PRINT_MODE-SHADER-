/*
    fugr_dump -> dump a portion of the map for 
    the testbed to work on.
*/
#include <set>
#include <map>
#include <utility>
#include "df/api.h"
#include "df/world.h"
#include "df/world_data.h"
#include "df/world_region_details.h"
#include "df/world_geo_biome.h"
#include "df/world_geo_layer.h"
#include "df/inclusion_type.h"
#include "df/inorganic_raw.h"
#include "df/plant_raw.h"
#include "df/creature_raw.h"
#include "df/historical_figure.h"
#include "df/map_block.h"
#include "df/block_square_event.h"
#include "df/block_square_event_frozen_liquidst.h"
#include "df/block_square_event_grassst.h"
#include "df/block_square_event_world_constructionst.h"
#include "df/block_square_event_mineralst.h"
#include "df/block_square_event_frozen_liquidst.h"
#include "df/building.h"
#include "df/building_def.h"
#include "df/item.h"
#include "df/unit.h"
#include "df/plant.h"
#include "df/construction.h"
#include "df/effect.h"
#include "df/cursor.h"
#include "df/global_objects.h"

static const uint16_t NONEMAT_NO = 0;
static const uint16_t SKIPMAT_NO  = 1;
static const uint16_t SOAPMAT_NO  = 2;

/*

map_block::designation : tile_designation[16][16]
    uint32_t geolayer_index : 4;
    uint32_t light : 1;
    uint32_t subterranean : 1;
    uint32_t outside : 1;
    uint32_t biome : 4;

map_block::region_offset : uint8_t[9]

so 
    region_offset[biome] points to a region
    while geolayer_index points to a layer within that region.

This complicates caching. Crap.




world->map.region_{xy} : coordinates of ? in embark squares(?). Most likely of the embark spot.
dfhack holds that:
    a region is 16x16 embark squares.
    

world->world_data->region_map[] - 2d map of regions in the world:
      int32_t region_id;
      int32_t landmass_id;
      int16_t geo_index;
    
world->world_data->region_details - region_details objects:
    int8_t biome[17][17];
    int16_t elevation[17][17];
    df::coord2d pos;
    int16_t elevation2[16][16];

world->world_data->regions : std::vector<df::world_region* >:
    df::language_name name;
    int32_t index;
    int16_t unk_70;
    df::coord2d_path region_coords;

*/


struct _bicache_item { // should be indices if we ever go 64bit, but...
    std::vector<df::building *> buildings;
    std::vector<df::item *> items;
    std::vector<df::unit *> units;
    std::vector<df::construction *> constructions;
    bool has_something;
    
    _bicache_item() { has_something = false; }
};

struct _bicache {

    std::vector<_bicache_item> head;
    df::coord origin;
    df::coord size;
    
    _bicache() {
        origin = { 0, 0 ,0};
        size = { 0, 0 ,0};
    }
    ~_bicache() { head.clear(); }
    
    void update(df::coord a, df::coord b) {
        if ( size < b - a ) {
            size = b - a;
        }

        head.reserve(size.x*size.y*size.z);
        for (int i=0; i<size.x*size.y*size.z; i++)
            head[i] = _bicache_item();
        
        for ( int i=0; i < df::global::world->items.all.size(); i++ ) {
            df::item *item  = df::global::world->items.all[i];
            if (item->pos.x != -30000) {
                at(item->pos/16).items.push_back(item);
                at(item->pos/16).has_something = true;
            }
        }
        for ( int i=0; i < df::global::world->buildings.all.size(); i++ ) {
            df::building& bu = *df::global::world->buildings.all[i];
            int sbx = bu.x1>>4, sby = bu.y1>>4;
            int ebx = bu.x2>>4, eby = bu.y2>>4;
            for (int j=sbx; j <= ebx; j++)
                for (int k=sby; k<=eby; k++) {
                    df::coord pos(j, k, bu.z);
                    at(pos/16).buildings.push_back(&bu);
                    at(pos/16).has_something = true;
                }
        }
        for (int i=0; i<df::global::world->units.all.size(); i++ ) {
            df::unit *unit = df::global::world->units.all[i];
            at(unit->pos/16).units.push_back(unit);
            at(unit->pos/16).has_something = true;
        }
        for (int i=0; i < df::global::world->constructions.size() ; i ++) {
            df::construction *c = df::global::world->constructions[i];
            at(c->pos/16).constructions.push_back(c);
            at(c->pos/16).has_something = true;
        }
    }
    
    inline _bicache_item& at(df::coord w) {
        return head[w.z*size.x*size.y + w.y*size.x +w.x];
    }
    inline _bicache_item& at(int x, int y, int z) {
        return head[z*size.x*size.y + y*size.x +x];
    }
};

struct _mat {
    enum { BUILTIN, INORGANIC, PLANT }  klass;
    uint16_t num; 
    int16_t type; 
    int32_t index; 
    std::string id;
};
static const char *_mat_klassname[] = { "BUILTIN", "INORGANIC", "PLANT", NULL };
static std::vector<_mat> mats_list;
static std::set< std::pair<int16_t, int32_t> > soap_set;
static std::map<std::pair<int16_t, int32_t>, _mat> mats_map;

static uint16_t _map_mat(const int16_t type, const int32_t index) {
    if ((type >=19) and (type < 419)) { /* creature-race. return soap or skip */
        if (soap_set.end() == soap_set.find(std::make_pair(type, index)))
            return SKIPMAT_NO;
        else
            return SOAPMAT_NO;
    }
    // at this point only builtins, inorganics and plants are left
    std::map<std::pair<int16_t, int32_t>, _mat>::iterator it = mats_map.find(std::make_pair(type, index));
    if (it == mats_map.end())
        return NONEMAT_NO;
    return (*it).second.num;
}
// adds only to mapping
static void _add_mat(const _mat& mat, const int16_t type, const int32_t index) {
    switch (mat.num) {
        case SOAPMAT_NO:
            soap_set.insert(std::make_pair(type, index));
            return;
        case SKIPMAT_NO: // implicit
            return;
        case NONEMAT_NO: // implicit
            return;
        default:
            mats_map.insert(std::make_pair(std::make_pair(type, index), mat));
            return;
    }
}

// adds to both list and mapping
static uint16_t _list_mat(_mat mat) {
    mats_list.push_back(mat);
    _add_mat(mat, mat.type, mat.index);
    return mats_list.size();
}

static void matstats(FILE *fp) {
    fprintf(fp, "matstats: %d mats listed\n", mats_list.size()); 
    fprintf(fp, "matstats: %d soap pairs\n",  soap_set.size()); 
    fprintf(fp, "matstats: %d mapped pairs\n",  mats_map.size()); 
}
static void fputmat(const struct _mat& m, FILE *fp) {
    fprintf(fp, "%hu type=%hd index=%d klass=%s id=%s\n", m.num, 
            m.type, m.index, _mat_klassname[m.klass], m.id.c_str());
}
static void dump_materials(FILE *fp) {
    fputs("section:materials\n", fp);
    for (int i=0; i < mats_list.size(); i++)
        fputmat(mats_list[i], fp);
    matstats(stderr);
}
static void init_materials(void) {
    std::vector<df::creature_raw*>& creas = df::creature_raw::get_vector();
    std::vector<df::inorganic_raw*>& inorgs = df::inorganic_raw::get_vector();
    std::vector<df::plant_raw*>& plants = df::plant_raw::get_vector();
    const int builtin_mats = 659;
    df::material **builtins =  df::global::world->raws.mat_table.builtin;
    
    // add nonmat rec
    _mat nonmat = { _mat::BUILTIN, NONEMAT_NO, -1, -1, "NONEMAT" };
    uint16_t matnum = _list_mat(nonmat);
    
    // skipmat - stub for those mats that shouldn't have tiles made of them
    _mat skipmat = { _mat::BUILTIN, matnum, -1, -1, "SKIPMAT" };
    matnum = _list_mat(skipmat);
    
    // soapmat - stub for all kinds of soap.
    _mat soapmat = { _mat::BUILTIN, matnum, -1, -1, "SOAP" };
    matnum = _list_mat(soapmat);
    
    int16_t type;
    int32_t index;
    std::string bina = "none";
    type = 0;
    for (index = 0; index < inorgs.size(); index ++) {
        _mat inomat = { _mat::INORGANIC, matnum, type, index, inorgs[index]->id };
        matnum = _list_mat(inomat);
    }

    index = -1;
    for (type = 0; type < builtin_mats; type ++) {
        if (type == 7) {
            _mat coke = { _mat::BUILTIN, matnum, type, 0, "COKE" };
            matnum = _list_mat(coke);
            _mat charcoal = { _mat::BUILTIN, matnum, type, 1, "CHARCOAL" };
            matnum = _list_mat(charcoal);
        }
        if (builtins[type]) {
            _mat bimat = { _mat::BUILTIN, matnum, type, index, builtins[type]->id };
            matnum = _list_mat(bimat);
        }
    }
    
    for (index = 0; index < creas.size(); index ++) 
        for (int16_t subtype = 0; subtype < creas[index]->material.size(); subtype ++) {
            int16_t type = subtype + 19;
            std::string& subid = creas[index]->material[subtype]->id;
            if ( subid == "SOAP")
                _add_mat(soapmat, type, index);
            else
                _add_mat(skipmat, type, index);
        }

    std::vector<df::historical_figure*>& histfigs = df::historical_figure::get_vector();
    for (index = 0; index < histfigs.size(); index ++) {
        df::creature_raw *crea = df::creature_raw::find(histfigs[index]->race);
        if (!crea)
            continue;
        for (int subtype = 0; subtype < crea->material.size(); subtype ++) {
            int16_t type = subtype + 219;
            std::string& subid = crea->material[subtype]->id;
            if ( subid == "SOAP")
                _add_mat(soapmat, type, index);
            else
                _add_mat(skipmat, type, index);
        }
    }

    for (index = 0; index < plants.size(); index ++)
        for (int16_t subtype = 0; subtype < plants[index]->material.size(); subtype ++) {
            int16_t type = subtype + 419;
            std::string& subid = plants[index]->material[subtype]->id;
            if ( ( subid == "STRUCTURAL") or ( subid == "WOOD") ) {
                std::string id;
                id += plants[index]->id;
                id += " subklass=";
                id += subid;
                _mat plamat = { _mat::PLANT, matnum, type, index, id };
                matnum = _list_mat(plamat);
            } else if ( subid == "SOAP" )
                _add_mat(soapmat, type, index);
        }
}

static void dump_constructions(FILE *fp) {
    fputs("section:constructions\n", fp);
    for (int i=0; i < df::global::world->constructions.size() ; i ++) {
        df::construction& c = *df::global::world->constructions[i];
        fprintf(fp, "%hd,%hd,%hd item_type=%hd item_subtype=%hd mat=(%hd, %d) matnum=%hd orig_tile=%hd\n",
            c.pos.x, c.pos.y, c.pos.z, c.item_type, c.item_subtype, c.mat_type, c.mat_index, 
            _map_mat(c.mat_type, c.mat_index), c.original_tile);
    }
}

static void dump_buildings(FILE *fp) {
    fputs("section:buildings\n", fp);
    
    for ( int i=0; i < df::global::world->buildings.all.size(); i++ ) {
        df::building& b = *df::global::world->buildings.all[i];
        fprintf(fp, 
            "%d:%d:%d %d:%d:%d %d:%d:%d "
            "id=%d type=%s subtype=%d custype=%d "
            "mat=(%hd, %d) matnum=%d\n",
            b.x1, b.y1, b.z, 
            b.x2, b.y2, b.z, 
            b.centerx, b.centery, b.z,
            b.id, df::enums::building_type::get_key(b.getType()),
            b.getSubtype(), b.getCustomType(),
            b.mat_type, b.mat_index, _map_mat(b.mat_type, b.mat_index));
    }
}

static void dump_items(FILE *fp) {
    fputs("section:items\n", fp);
    
    std::string desc;
    for ( int i=0; i < df::global::world->items.all.size(); i++ ) {
        df::item& it = *df::global::world->items.all[i];
        if (it.pos.x + it.pos.y + it.pos.z < 0)
            continue;
        desc.clear();
        it.getItemDescription(&desc, 255);
        fprintf(fp, "%hd:%hd:%hd id=%d "
                    "type=%s subtype=%hd "
                    "mat=(%hd, %d) matnum=%hd actmat=(%hd, %d) actmatnum=%hd "
                    "desc=%s\n", 
            it.pos.x, it.pos.y, it.pos.z, it.id, 
            df::enums::item_type::get_key(it.getType()), 
            it.getSubtype(), it.getMaterial(), it.getMaterialIndex(),
            _map_mat(it.getMaterial(), it.getMaterialIndex()),
            it.getActualMaterial(), it.getActualMaterialIndex(),
            _map_mat(it.getActualMaterial(), it.getActualMaterialIndex()), desc.c_str() );
    }
}

static void dump_units(FILE *fp) {
    fputs("section:units\n", fp);
    
    for (int i=0; i<df::global::world->units.all.size(); i++ ) {
        df::unit& u = *df::global::world->units.all[i];
        fprintf(fp, "%hd:%hd:%hd "
            "id=%d name='%s' nickname='%s' "
            "prof=%hd,%hd custom_prof='%s' "
            "race=%d sex=%hhd caste=%hd\n",  
            u.pos.x, u.pos.y, u.pos.z, 
            u.id, u.name.first_name.c_str(), u.name.nickname.c_str(), 
            u.profession, u.profession2, u.custom_profession.c_str(), 
            u.race, u.sex, u.caste);
    }
}

static void dump_building_defs(FILE *fp) {
    fputs("section:building_defs\n", fp);
    
    df::world_raws& raws = df::global::world->raws;
    
    for (int i=0; i < raws.buildings.all.size() ; i ++) {
        df::building_def& def = *raws.buildings.all[i];
        fprintf(fp, "id=%d name=%s name_color=%hd,%hd,%hd,%hd build_key=%d "
                    "dim=%d:%d workloc=%d:%d build_stages=%d\n",
                def.id, def.name.c_str(), 
                def.name_color[0],
                def.name_color[1],
                def.name_color[2],
                def.name_color[3],
                def.build_key,
                def.dim_x, def.dim_y, 
                def.workloc_x, def.workloc_y,
                def.build_stages );
    }
}

struct layer_materials_cache {
    uint16_t mtab[256];

    uint16_t get(df::tile_designation des, uint8_t *regoff) {
        // TODO: check if des.bits.biome != regoff[des.bits.biome]
        // it's quite suspicious.
	return mtab[regoff[des.bits.biome] << 4 | des.bits.geolayer_index ];
    }
    
    layer_materials_cache() { // ala ReadGeology. Requires init_materials() having been run.

	int xmax = df::global::world->world_data->world_width - 1;
	int ymax = df::global::world->world_data->world_height - 1;
	
	for (int i = 0; i < 256; i++)
	    mtab[i] = NONEMAT_NO;
	
	for (int i = 0; i < 9; i++)
	{
	    // check against worldmap boundaries, fix if needed
	    // regionX is in embark squares
	    // regionX/16 is in 16x16 embark square regions
	    // i provides -1 .. +1 offset from the current region
	    int rx = df::global::world->map.region_x / 16 + ((i % 3) - 1);
	    int ry = df::global::world->map.region_y / 16 + ((i / 3) - 1);
	    
	    rx =  rx < 0 ? 0 : ( rx > xmax ? xmax : rx);
	    ry =  ry < 0 ? 0 : ( ry > ymax ? ymax : ry);
	    
	    uint16_t geo_index = df::global::world->world_data->region_map[rx][ry].geo_index;

	    df::world_geo_biome *geo_biome = df::world_geo_biome::find(geo_index);
	    if (!geo_biome)
		continue;
	    
	    // this requires no more than 16 layers per biome or data corruption will ensue
	    // and no more that 256 layers per biome or segfault becomes imminent.
            // layer stone, being stone is always type=0
	    for (int g = 0; g < geo_biome->layers.size(); g++ ) {
		mtab[ i<<4 | g] =  _map_mat(0, geo_biome->layers[g]->mat_index);
	    }
	}
    }
};
static bool constructed_tiles[df::enums::tiletype::_last_item_of_tiletype] = { false };
static void init_enumtabs() {
    if (not constructed_tiles[df::enums::tiletype::tiletype::ConstructedRamp]) {
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedFloor] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedFortification] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedPillar] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallRD2] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallR2D] = true,
        constructed_tiles[df::enums::tiletype::tiletype:: ConstructedWallR2U] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallRU2] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallL2U] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLU2] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallL2D] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLD2] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLRUD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallRUD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLRD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLRU] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLUD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallRD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallRU] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLU] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallUD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedWallLR] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedStairUD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedStairD] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedStairU] = true,
        constructed_tiles[df::enums::tiletype::tiletype::ConstructedRamp] = true;
    }

        
}

struct block_row {
    uint32_t *row;
    long size;
    int hrsize;
    
    block_row(int width) {
        hrsize = width;
        size = sizeof(uint32_t) * 4 * width * 256;
        row = static_cast<uint32_t *>(calloc(size, 1));
    }
    ~block_row() { free(row); }
    
    void write(long& offset, FILE *fp) {
        fseek(fp, offset, SEEK_SET);
        fwrite(row, size, 1, fp);
        offset += size;
        memset(row, 0, size);
    }
    inline void set(int x, int t_x, int t_y, 
                    uint16_t stone, uint16_t tiletype, 
                    uint16_t bmat, uint16_t building, 
                    uint16_t grass, int8_t grass_amount, 
                    uint32_t designation) {
        uint32_t *p = row + (hrsize*t_y*16 + x*16 + t_x)*4;
        *(p + 0) = ( (stone & 0xffff) << 16 ) | (tiletype & 0xffff);
        *(p + 1) = ( (bmat  & 0xffff) << 16 ) | (building & 0xffff);
        *(p + 2) =   (grass_amount << 16 )    | (   grass & 0xffff);
        *(p + 3) = designation;
    }    
};
void fugr_dump(void) {
    if (!df::global::world || !df::global::world->world_data) {
        fprintf(stderr, "World data is not available.: %p\n", df::global::world);
        return;
    }
    init_enumtabs();
    init_materials();
    layer_materials_cache lmc;
     
    std::vector<df::map_block* > map_blocks = df::global::world->map.map_blocks;
    df::map_block**** block_index = df::global::world->map.block_index;
    
    int32_t x_count_block = df::global::world->map.x_count_block;
    int32_t y_count_block =  df::global::world->map.y_count_block;
    int32_t z_count_block =  df::global::world->map.z_count_block;

    df::coord start( 0, 0, 0 );
    df::coord end( x_count_block, y_count_block, z_count_block);
    _bicache beecee;
    beecee.update(start, end);
    
    long map_offset, eff_offset;
    block_row row(end.x - start.x);
    
    FILE *fp = fopen("fugr.dump", "w");
    {
        {   /* reserve space for the header, keeping it human-readable. 52*3 = 156*/
            char filler[53];
            memset(filler, 32, sizeof(filler)-1);
            filler[sizeof(filler) - 1] = 10;
            for (int i = 0; i < 3; i ++)
                fwrite(filler, sizeof(filler), 1, fp);
        }
        /* dump stuff */
        dump_materials(fp);
        dump_buildings(fp);
        dump_constructions(fp);
        dump_building_defs(fp);
        dump_items(fp);
        dump_units(fp);

        // 64K-align map data posn, calc other offsets        
#define ALIGN64K(val) ( ( ((val)>>16) + 1 ) << 16 )
        fprintf(stderr, "row.size=%ld data_size=%ld", row.size, row.size * (end.y - start.y) * (end.z - start.z));
        const long data_size = ALIGN64K(row.size * (end.y - start.y) * (end.z - start.z));
        map_offset = ALIGN64K(ftell(fp));
        eff_offset = map_offset + data_size;
#undef ALIGN64K
        
        // write header
        fseek(fp, 0, SEEK_SET);
        fprintf(fp, "origin:%d:%d:%d\n", start.x, start.y, start.z);  // max 40 chars
        fprintf(fp, "extent:%d:%d:%d\n", (end.x - start.x), (end.y - start.y), (end.z - start.z)); // same
        fprintf(fp, "tiles:%ld:%ld\n", map_offset, data_size); // max 49 on x86_64
        fprintf(fp, "effects:%ld\n", eff_offset); // max 29 on x86_64. 158 max total 

        // write effects section header
        const char *esh = "\nsection:effects\n";
        fseek(fp, eff_offset, SEEK_SET);
        eff_offset += fwrite(esh, 1, strlen(esh), fp);
    }

    uint16_t building_mats[256];
    uint16_t building_tiles[256];
    memset(building_mats, 0, 512);
    memset(building_tiles, 0, 512);
    bool buildings_clean = true;
    
    uint16_t plant_mats[256];
    memset(plant_mats, 0, 512);
    bool plants_clean = true;
    std::vector<df::plant_raw*>& plant_raws = df::plant_raw::get_vector();
    
    for(int z = start.z; z < end.z; z++)
        for(int y = start.y; y < end.y; y++) {
            for(int x = start.x; x < end.x; x++) {
                df::map_block *b = block_index[x][y][z];
                if (b) {
                    if (not plants_clean) {
                        memset(plant_mats, 0, 512);
                        plants_clean = true;
                    }                    
                    if (not buildings_clean) {
                        memset(building_mats, 0, 512);
                        memset(building_tiles, 0, 512);                        
                        buildings_clean = true;
                    }
                    /* index block-local plant materials */
                    for (int pi = 0; pi < b->plants.size(); pi ++) {
                        plants_clean = false;
                        df::coord2d pc = b->plants[pi]->pos % 16;
                        /* this is an index straight into plant_raws */
                        int16_t material = b->plants[pi]->material;
                        /* for the sake of uniformity we look up basic_mat type/idx */
                        plant_mats[pc.x + 16*pc.y] = _map_mat(
                            plant_raws[material]->material_defs.type_basic_mat,
                            plant_raws[material]->material_defs.idx_basic_mat );
                    }
                    /* look if we've got buildings here */
                    _bicache_item& bi = beecee.at(x,y,z);
                    for (int i=0; i < bi.buildings.size(); i++) {
                        buildings_clean = false;
                        building_tiles[i] = bi.buildings[i]->getType() + 768; // trees, walls and other shit are <768.
                        building_mats[i] = _map_mat(bi.buildings[i]->mat_type, bi.buildings[i]->mat_index);
                    }
                    
                    /* dump effects */
                    for (int i=0; i< b->effects.size(); i++) {
                        df::effect &e = *b->effects[i];
                        fseek(fp, eff_offset, SEEK_SET);
                        eff_offset += fprintf(fp, "%hd,%hd,%hd effect type=%s mat=(%hd, %d) density=%hd\n",
                            e.x, e.y, e.z, df::enums::effect_type::get_key(e.type), e.mat_type, e.mat_index, e.density);
                    }
                    
                    for (int ti = 0; ti < 256; ti++) {
                        int t_x = ti % 16;
                        int t_y = ti / 16;
                        df::coord pos( x*16 + t_x, y*16 + t_y, z );
                        df::tiletype tiletype = b->tiletype[t_x][t_y];

                        uint16_t tile_mat = lmc.get(b->designation[t_x][t_y], b->region_offset);
                        uint16_t grass_mat = 0;
                        int8_t   grass_amt = 0;
                        uint16_t building_tile = 0;
                        uint16_t building_mat = 0;
                        uint16_t worldconstr_mat = 0;
                        /* gather materials */
                        for (int i = 0; i < b->block_events.size(); i++)
                            switch (b->block_events[i]->getType()) {
                                case df::block_square_event_type::mineral:
                                {
                                   df::block_square_event_mineralst *e = (df::block_square_event_mineralst *)b->block_events[i];
                                    if ( e->tile_bitmask[t_y] & ( 1 << t_x  ) )
                                        tile_mat = _map_mat(0, e->inorganic_mat);
                                    break;
                                }
                                case df::block_square_event_type::world_construction:
                                {
                                    df::block_square_event_world_constructionst *e = (df::block_square_event_world_constructionst *)b->block_events[i];
                                    if ( e->tile_bitmask[t_y] & ( 1 << t_x  ) ) {
                                        worldconstr_mat = e->inorganic_mat;
                                    }
                                    fprintf(stderr,"wconstr at %hd %hd %hd %d mat=(0, %d)\n", pos.x, pos.y, pos.z, 
                                        e->tile_bitmask[t_y] & ( 1 << t_x  ), e->inorganic_mat);
                                    break;
                                }
                                case df::block_square_event_type::grass:
                                {
                                    df::block_square_event_grassst *e = (df::block_square_event_grassst *)b->block_events[i];
                                    grass_amt = e->amount[t_x][t_y];
                                    grass_mat =_map_mat( plant_raws[e->plant_index]->material_defs.type_basic_mat,
                                                       plant_raws[e->plant_index]->material_defs.idx_basic_mat );                                   
                                    break;
                                }
        
                                case df::enums::block_square_event_type::frozen_liquid:
                                {
                                    df::block_square_event_frozen_liquidst *e = (df::block_square_event_frozen_liquidst *)b->block_events[i];
                                    if (e->tiles[t_x][t_y]) {
                                        fseek(fp, eff_offset, SEEK_SET);
                                        eff_offset += fprintf(fp, "%hd:%hd:%hd frozen tile=%hd liquid_type=%hhd tiletype=%hd\n",
                                            pos.x, pos.y, pos.z, e->tiles[t_x][t_y], e->liquid_type[t_x][t_y].value, tiletype);
                                    }
                                    break;
                                }
        #if 0
                                case df::enums::block_square_event_type::material_spatter:
                                    e = (df::block_square_event_material_spatterst *)mablo->block_events[i];
                                    break;
        #endif
                                default:
                                    break;
                            }
                        
                        if (constructed_tiles[tiletype]) {
                            bool done = false;
                            for (int i = 0; i < bi.constructions.size(); i++) {
                                df::construction& suspect = *bi.constructions[i];
                                if (pos == suspect.pos) {
                                    building_mat = _map_mat(suspect.mat_type, suspect.mat_index);
                                    building_tile = tiletype;
                                    tiletype = static_cast<df::enums::tiletype::tiletype>(suspect.original_tile);
                                    done = true;
                                    break;
                                }
                            }
                            if (not done)
                                fprintf(stderr, "Whoa, constructed tile, but no corresponding construction.\n"
                                    "pos %hd:%hd:%hd constructions in bicache: %d\n", pos.x, pos.y, pos.z,
                                       bi.constructions.size() );
                        }
                        if (plant_mats[ti]) {
                            building_mat = plant_mats[ti];
                            building_tile = 0;
                        }
                        if (building_mats[ti]) {
                            building_mat = building_mats[ti];
                            building_tile = building_tiles[ti];;
                        }
                        
                        row.set(x, t_x, t_y, 
                                tile_mat, tiletype, 
                                building_mat, building_tile, 
                                grass_mat, grass_amt,
                                b->designation[t_x][t_y].whole);
                    }
                }
            }
            row.write(map_offset, fp);
        }
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    fputs("dumped, flushed and synced.\n", stderr);
    exit(0);
}
