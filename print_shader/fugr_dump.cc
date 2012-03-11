/*
    fugr_dump -> dump a portion of the map for 
    the testbed to work on.
*/
#include <map>
#include "df/api.h"
#include "df/world.h"
#include "df/world_data.h"
#include "df/world_region_details.h"
#include "df/world_geo_biome.h"
#include "df/world_geo_layer.h"
#include "df/inclusion_type.h"
#include "df/inorganic_raw.h"
#include "df/plant_raw.h"
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


static const uint32_t NOMAT = 0xFFFFFFFFu;

struct layer_materials_cache {
    uint32_t mtab[256];

    uint32_t get(df::tile_designation des, uint8_t *regoff) {
        // TODO: check if des.bits.biome != regoff[des.bits.biome]
        // it's quite suspicious.
	return mtab[regoff[des.bits.biome] << 4 | des.bits.geolayer_index ];
    }
    
    layer_materials_cache() { // ala ReadGeology

	int xmax = df::global::world->world_data->world_width - 1;
	int ymax = df::global::world->world_data->world_height - 1;
	
	for (int i = 0; i < 256; i++)
	    mtab[i] = NOMAT;
	
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
	    for (int g = 0; g < geo_biome->layers.size(); g++ ) {
		mtab[ i<<4 | g] =  geo_biome->layers[g]->mat_index;
	    }
	}
    }
};

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
};

struct _bicache {

    _bicache_item *head;
    df::coord origin;
    df::coord size;
    
    _bicache() {
        origin = { 0, 0 ,0};
        size = { 0, 0 ,0};
        head = NULL;
    }
    ~_bicache() { if (head) free(head); }
    
    void update(df::coord a, df::coord b) {
        if ( size < b-a ) {
            if (head)
                free(head);
            size = b - a;
            head = static_cast<_bicache_item*> (calloc(sizeof(_bicache_item), size.x*size.y*size.z));
        }
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
        return *(head + w.z*size.x*size.y + w.y*size.x +w.x);
    }
    inline _bicache_item& at(int x, int y, int z) {
        return *(head + z*size.x*size.y + y*size.x +x);
    }
};

static void dump_buildings(FILE *fp, _bicache& cache) {
    fputs("section:buildings\n", fp);
    
    for ( int i=0; i < df::global::world->buildings.all.size(); i++ ) {
        df::building& b = *df::global::world->buildings.all[i];
        fprintf(fp, 
            "%d,%d,%d %d,%d,%d, %d,%d,%d, "
            "id=%d name=%s type=%d subtype=%d custype=%d "
            "mat_type=%hd mat_index=%d\n",
            b.x1, b.y1, b.z, b.x2, b.y2, b.z, b.centerx, b.centery, b.z,
            b.id, b.name.c_str(), b.getType(), b.getSubtype(), b.getCustomType(),
            b.mat_type, b.mat_index);
    }
}

static void dump_items(FILE *fp, _bicache& cache) {
    fputs("section:items\n", fp);
    
    std::string desc;
    for ( int i=0; i < df::global::world->items.all.size(); i++ ) {
        df::item& it = *df::global::world->items.all[i];
        if (it.pos.x + it.pos.y + it.pos.z < 0)
            continue;
        desc.clear();
        it.getItemDescription(&desc, 255);
        fprintf(fp, "%hd,%hd,%hd id\%d type=%hd subtype=%hd actmat=%hd actmatindex=%d"
            "desc=%s\n", it.pos.x, it.pos.y, it.pos.z,
            it.id, it.getType(), it.getSubtype(),
            
            it.getActualMaterial(), it.getActualMaterialIndex(), desc.c_str() );
    }
}

static void dump_units(FILE *fp, _bicache& cache) {
    fputs("section:units\n", fp);
    
    for (int i=0; i<df::global::world->units.all.size(); i++ ) {
        df::unit& u = *df::global::world->units.all[i];
        fprintf(fp, "%hd,%hd,%hd "
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
                    "dim=%d,%d workloc=%d,%d build_stages=%d\n",
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

static void dump_materials(FILE *fp) {
    fputs("section:materials\n", fp);
    // dumping inorg and plant maps
    std::vector<df::inorganic_raw* > inorg = df::global::world->raws.inorganics;
    std::vector<df::plant_raw* > plants = df::global::world->raws.plants.all;    
    
    for (int i=0; i < inorg.size(); i++)
	fprintf(fp, "%d INORG %s\n", i, inorg[i]->id.c_str());
    for (int i=0; i < plants.size(); i++)
	fprintf(fp, "%d PLANT %s\n", i, plants[i]->id.c_str());
    fprintf(fp, "%d ICE %s\n", 0x3fe, "frozen water");
}

static void dump_constructions(FILE *fp) {
    fputs("section:constructions\n", fp);
    for (int i=0; i < df::global::world->constructions.size() ; i ++) {
        df::construction& c = *df::global::world->constructions[i];
        fprintf(fp, "%hd,%hd,%hd item_type=%hd item_subtype=%hd mat_type=%hd mat_index=%d orig_tile=%hd\n",
            c.pos.x, c.pos.y, c.pos.z, c.item_type, c.item_subtype, c.mat_type, c.mat_index, c.original_tile);
    }
}


static bool constructed_tiles[df::enums::tiletype::_last_item_of_tiletype] = { false };
static bool no_material_tiles[df::enums::tiletype::_last_item_of_tiletype] = { false };
static inline void hash_and_write(int hr_width, int b_x, int t_x, int t_y, uint16_t mat, df::tiletype tt, uint32_t *hashed_row) {
    int rti = hr_width*16*t_y +  b_x*16 + t_x;
    hashed_row[rti] = ((mat & 0x3ff)<<10) | (tt & 0x3ff);
}
static inline void liquid_write(int hr_width, int b_x, int t_x, int t_y, uint32_t desn, uint32_t *liquid_row) {
    int rti = hr_width*16*t_y +  b_x*16 + t_x;
    liquid_row[rti] = desn;
}

void fugr_dump(void) {
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
    if (not no_material_tiles[df::enums::tiletype::tiletype::Void]) {
        no_material_tiles[df::enums::tiletype::tiletype::Void] = true;
        no_material_tiles[df::enums::tiletype::tiletype::RampTop] = true;
        no_material_tiles[df::enums::tiletype::tiletype::Driftwood] = true;
        no_material_tiles[df::enums::tiletype::tiletype::OpenSpace] = true;
        no_material_tiles[df::enums::tiletype::tiletype::Campfire] = true;
        no_material_tiles[df::enums::tiletype::tiletype::Chasm] = true;
        no_material_tiles[df::enums::tiletype::tiletype::Ashes1] = true;
        no_material_tiles[df::enums::tiletype::tiletype::Ashes2] = true;
        no_material_tiles[df::enums::tiletype::tiletype::Ashes3] = true;
        no_material_tiles[df::enums::tiletype::tiletype::EeriePit] = true;
        no_material_tiles[df::enums::tiletype::tiletype::Fire] = true;
        no_material_tiles[df::enums::tiletype::tiletype::GlowingBarrier] = true;
        no_material_tiles[df::enums::tiletype::tiletype::GlowingFloor] = true;
        no_material_tiles[df::enums::tiletype::tiletype::MagmaFlow] = true;
        no_material_tiles[df::enums::tiletype::tiletype::Waterfall] = true;
        //no_material_tiles[df::enums::tiletype::tiletype::] = true;
    }
        
    if (!df::global::world || !df::global::world->world_data)
    {
        fprintf(stderr, "World data is not available.: %p\n", df::global::world);
        return;
    }
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
    
    
    long des_offset, map_offset, eff_offset;
    FILE *fp = fopen("fugr.dump", "w");
    {
        const int header_size = 256;
        long bindata_size = (((x_count_block * y_count_block * z_count_block *256 * 4) >>12) + 1) <<12;
        fseek(fp, header_size, SEEK_SET); // reserve space for the header.
        /* dump stuff */
        dump_materials(fp);
        dump_buildings(fp, beecee);
        dump_constructions(fp);
        dump_building_defs(fp);
        dump_items(fp, beecee);
        dump_units(fp, beecee);

        // 64K-align map data posn, calc other offsets        
#define ALIGN64K(val) ( ( ((val)>>16) + 1 ) << 16 )
        map_offset = ALIGN64K(ftell(fp));
        des_offset = map_offset + ALIGN64K(bindata_size);
        eff_offset = des_offset + ALIGN64K(bindata_size);
#undef ALIGN64K
        
        // write header
        fseek(fp, 0, SEEK_SET);
        fprintf(fp, "blocks:%d:%d:%d\n", x_count_block, y_count_block, z_count_block);
        fprintf(fp, "tiles:%ld:%ld\n", map_offset, bindata_size);
        fprintf(fp, "designations:%ld:%ld\n", des_offset, bindata_size);
        fprintf(fp, "effects:%ld\n", eff_offset);

        // write effects section header
        fseek(fp, eff_offset, SEEK_SET);
        fputs("section:effects\n", fp);
    }

    /* hashed row is a set of 16 rows of tile hashes
       ready to be blitted, dumped or whatever.
       Memory layout:
       000000000000000011111111111111112222222222222222 ..... eeee
       000000000000000011111111111111112222222222222222 ..... eeee
       ... 16 rows.
       
       Hashing goes block by block. Given block's x coord and t_x, t_y 
       tile coords inside it, corresponding tilehash in the hashed_row is:
       width*16*t_y +  (x*16+t_x) where width is number of blocks in the row.
    */
    
    int hr_width = end.x - start.x;
    uint32_t stone_row[256*hr_width];
    uint32_t liquid_row[256*hr_width]; /* unused|raining|salty|stagnant|magma|amount:3 */
    
    uint16_t plant_mats[256];
    
    bool plants_clean = true;
    for(int z = start.z; z < end.z; z++)
        for(int y = start.y; y < end.y; y++) {
            memset(stone_row, 0xFF, sizeof(stone_row));
            memset(liquid_row, 0, sizeof(liquid_row));
            for(int x = start.x; x < end.x; x++) {
                df::map_block *b = block_index[x][y][z];
                if (b) {
                    if (not plants_clean) {
                        memset(plant_mats, 0xFF, 512);
                        plants_clean = true;
                    }
                    /* index block-local plant materials */
                    for (int pi = 0; pi < b->plants.size(); pi ++) {
                        plants_clean = false;
                        df::coord2d pc = b->plants[pi]->pos % 16;
                        plant_mats[pc.x + 16*pc.y] = b->plants[pi]->material;
                    }
                    /* look if we've got buildings here */
                    _bicache_item& bi = beecee.at(x,y,z);
                    for (int i=0; i < bi.buildings.size(); i++) {
                        /* render building */
                    }
                    /* same for items and creatures */
                    
                    /* dump effects */
                    for (int i=0; i< b->effects.size(); i++) {
                        df::effect &e = *b->effects[i];
                        fseek(fp, eff_offset, SEEK_SET);
                        eff_offset += fprintf(fp, "%hd,%hd,%hd type=%hd idx=%d density=%hd\n",
                            e.x, e.y, e.z, e.mat_type, e.mat_index, e.density);
                    }
                    
                    for (int ti = 0; ti < 256; ti++) {
                        int t_x = ti % 16;
                        int t_y = ti / 16;
                        df::coord pos( x*16 + t_x, y*16+t_y, z );
                        df::tiletype tiletype = b->tiletype[t_x][t_y];
                        
                        { /* gather liquids; all designations in fact. */
                            uint32_t l = b->designation[t_x][t_y].whole;
                            liquid_write(hr_width, x, t_x, t_y, l, liquid_row);
                        }
                        if (no_material_tiles[tiletype]) {
                            hash_and_write(hr_width, x, t_x, t_y, NOMAT, tiletype, stone_row);
                            continue;
                        }
                        if (plant_mats[ti] != 0xffff) {
                            hash_and_write(hr_width, x, t_x, t_y, plant_mats[ti], tiletype, stone_row);
                            continue;
                        }
                        if (constructed_tiles[tiletype]) {
                            bool done = false;
                            for (int i = 0; i < bi.constructions.size(); i++) {
                                df::construction& suspect = *bi.constructions[i]; 
                                if (pos == suspect.pos) {
                                    hash_and_write(hr_width, x, t_x, t_y, suspect.mat_index, tiletype, stone_row);
                                    done = true;
                                    break;
                                }
                            }
                            if (done)
                                continue;
                            fprintf(stderr, "Whoa, constructed tile, but no corresponding construction.\n"
                                "pos %hd:%hd:%hd constructions in bicache: %d\n", pos.x, pos.y, pos.z,
                                   bi.constructions.size() );
                        }
                            
                        int mat = lmc.get(b->designation[t_x][t_y], b->region_offset);                        
                        for (int i = 0; i < b->block_events.size(); i++)
                            switch (b->block_events[i]->getType()) {
                                case df::block_square_event_type::mineral:
                                {
                                   df::block_square_event_mineralst *e = (df::block_square_event_mineralst *)b->block_events[i];
                                    if ( e->tile_bitmask[t_y] & ( 1 << t_x  ) )
                                        mat = e->inorganic_mat;
                                    break;
                                }
                                case df::block_square_event_type::world_construction:
                                {
                                    df::block_square_event_world_constructionst *e = (df::block_square_event_world_constructionst *)b->block_events[i];
                                    if ( e->tile_bitmask[t_y] & ( 1 << t_x  ) ) {
                                        mat = e->inorganic_mat;
                                    }
                                    fprintf(stderr,"constr at %hd %hd %hd %d mat=%d\n", pos.x, pos.y, pos.z, 
                                        e->tile_bitmask[t_y] & ( 1 << t_x  ), e->inorganic_mat);
                                    break;
                                }
                                case df::block_square_event_type::grass:
                                {
                                    df::block_square_event_grassst *e = (df::block_square_event_grassst *)b->block_events[i];
                                    if ((e->amount[t_x][t_y] > 0) )
                                        mat = e->plant_index;
                                    break;
                                }
        
                                case df::enums::block_square_event_type::frozen_liquid:
                                {
                                    df::block_square_event_frozen_liquidst *e = (df::block_square_event_frozen_liquidst *)b->block_events[i];
                                    if (e->tiles[t_x][t_y])
                                        fprintf(stderr, "%hd:%hd:%hd got frozen: tile %hd  liquid_type %hhd tt=%hd\n",
                                            pos.x, pos.y, pos.z, e->tiles[t_x][t_y], e->liquid_type[t_x][t_y].value, tiletype);
                                
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
                        hash_and_write(hr_width, x, t_x, t_y, mat, tiletype, stone_row);
                    }
                }
            }
            /* now dump entire hashed_row at once. */
            fseek(fp, map_offset, SEEK_SET);
            fwrite(stone_row, sizeof(stone_row), 1, fp);                
            fseek(fp, des_offset, SEEK_SET);
            fwrite(liquid_row, sizeof(liquid_row), 1, fp);
            map_offset += sizeof(stone_row);
            des_offset += sizeof(liquid_row);
        }
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    fputs("dumped, flushed and synced.\n", stderr);
}

