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

void fugr_dump(void) {
    
    if (!df::global::world || !df::global::world->world_data)
    {
        fprintf(stderr, "World data is not available.: %p\n", df::global::world);
        return;
    }
    layer_materials_cache lmc;
 
    fprintf(stderr, "cursor posn: %d:%d:%d\n", df::global::cursor->x, df::global::cursor->y, df::global::cursor->z);
    
    std::vector<df::map_block* > map_blocks = df::global::world->map.map_blocks;
    df::map_block**** block_index = df::global::world->map.block_index;
    
    int32_t x_count_block = df::global::world->map.x_count_block;
    int32_t y_count_block =  df::global::world->map.y_count_block;
    int32_t z_count_block =  df::global::world->map.z_count_block;
    int32_t x_count =  df::global::world->map.x_count;
    int32_t y_count =  df::global::world->map.y_count;
    int32_t z_count =  df::global::world->map.z_count;
    int32_t region_x =  df::global::world->map.region_x;
    int32_t region_y =  df::global::world->map.region_y;
    int32_t region_z =  df::global::world->map.region_z;   
    
    
    FILE *fp = fopen("fugrdump.mats", "w");
    
    fprintf(fp, "count_block: %d:%d:%d\ncount: %d:%d:%d\nregion: %d:%d:%d\n",
	x_count_block, y_count_block, z_count_block,
	x_count, y_count, z_count, 
	region_x, region_y, region_z );
    
    // dumping inorg and plant maps
    std::vector<df::inorganic_raw* > inorg = df::global::world->raws.inorganics;
    std::vector<df::plant_raw* > plants = df::global::world->raws.plants.all;    
    
    for (int i=0; i < inorg.size(); i++)
	fprintf(fp, "%d INORG %s\n", i, inorg[i]->id.c_str());
    for (int i=0; i < plants.size(); i++)
	fprintf(fp, "%d PLANT %s\n", i, plants[i]->id.c_str());
    fclose(fp);
    fp = fopen("fugrdump.tiles", "w");

    // dumping 6bx6bx6z at cursor coords.
    // that is 96x96x6 tiles
    const int dumpw = 16*6;
    const int dumph = 6;
    
    int c_xb = df::global::cursor->x / 16;
    int c_yb = df::global::cursor->y / 16;
    int c_z  = df::global::cursor->z;
    
    int x_start = (c_xb - 3) * 16; 
    int y_start = (c_yb - 3) * 16;
    int z_start = (c_z  - 3);
    
    // dumping the whole map

    x_start = 0;
    y_start = 0;
    z_start = 0;
    int x_end = x_count_block;
    int y_end = y_count_block;
    int z_end = z_count_block;
    

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
    int hr_width = x_end - x_start;
    uint32_t hashed_row[256*hr_width];
    for(int z = z_start; z < z_end; z++)
        for(int y = y_start; y < y_end; y++) {
            memset(hashed_row, 0xFF, 1024*hr_width);
            for(int x = x_start; x < x_end; x++) {
                df::map_block *b = block_index[x][y][z];
                if (b) {
                    for (int ti = 0; ti < 256; ti++) {
                        int t_x = ti % 16;
                        int t_y = ti / 16;
                        int mat = lmc.get(b->designation[t_x][t_y], b->region_offset);
                        for (int i = 0; i < b->block_events.size(); i++)
                            switch (b->block_events[i]->getType()) {
                                case df::block_square_event_type::mineral:
                                {
                                   df::block_square_event_mineralst *e = (df::block_square_event_mineralst *)b->block_events[i];
                                    if ( e->tile_bitmask[t_y] & ( 1 << t_x  )  && (mat == -1))
                                        mat = e->inorganic_mat;
                                    break;
                                }
                                case df::block_square_event_type::world_construction:
                                {
                                    df::block_square_event_world_constructionst *e = (df::block_square_event_world_constructionst *)b->block_events[i];
                                    if ( e->tile_bitmask[t_y] & ( 1 << t_x  ) )
                                        mat = e->inorganic_mat;
                                    break;
                                }
                                case df::block_square_event_type::grass:
                                {
                                    df::block_square_event_grassst *e = (df::block_square_event_grassst *)b->block_events[i];
                                    if ((e->amount[t_x][t_y] > 0) && (mat == -1))
                                        mat = e->plant_index;
                                    break;
                                }
        #if 0
                                case frozen_liquid:
                                    e = (df::block_square_event_frozen_liquidst *)mablo->block_events[i];
                                    break;
                                case material_spatter:
                                    e = (df::block_square_event_material_spatterst *)mablo->block_events[i];
                                    break;
        #endif
                                default:
                                    break;
                            }
                        int rti = hr_width*16*t_y +  x*16+t_x;
                        hashed_row[rti] = ((mat & 0x3ff)<<10) | (b->tiletype[t_x][t_y] & 0x3ff);
                    }
                }
            }
            /* now dump entire hashed_row at once. */
            fwrite(hashed_row, sizeof(uint32_t) * 256 * hr_width, 1, fp);                
        }
    fclose(fp);
}

