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

    uint32_t get(df::tile_designation des) {
	return mtab[( des.bits.geolayer_index << 4 ) | des.bits.biome];
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
	    
	    uint16_t geo_index = df::global::world->world_data->region_map[ry][ry].geo_index;

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
    
    fprintf(stderr, "map dim:\ncount_block: %d:%d:%d\ncount: %d:%d:%d\nregion: %d:%d:%d\n",
	x_count_block, y_count_block, z_count_block,
	x_count, y_count, z_count, 
	region_x, region_y, region_z );
    
    
    
    FILE *fp = fopen("fugr.dump", "w");
    
    // dumping inorg and plant maps
    std::vector<df::inorganic_raw* > inorg = df::global::world->raws.inorganics;
    std::vector<df::plant_raw* > plants = df::global::world->raws.plants.all;    
    
    for (int i=0; i < inorg.size(); i++)
	fprintf(fp, "%d INORG %s\n", i, inorg[i]->id.c_str());
    for (int i=0; i < plants.size(); i++)
	fprintf(fp, "%d PLANT %s\n", i, plants[i]->id.c_str());
    fprintf(fp, "now map\n");
    df::tiletype tt;
    df::map_block *mablo;
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
    
    
    for(int z = z_start; z - z_start < dumph; z++)
        for(int gt_y = y_start; gt_y -y_start < dumpw; gt_y++)
            for(int gt_x = x_start; gt_x - x_start < dumpw; gt_x++) {
		if ((gt_x < 0) || (gt_x > x_count - 1) || (gt_y < 0) || (gt_y > y_count -1)) {
		    fputs("nil\n", fp);
		    continue;
		}
		    
		uint32_t t_x = gt_x % 16;
		uint32_t t_y = gt_y % 16;
		uint32_t t_z = z;
		
		if ( ( t_x == 0) && (t_y == 0) )
		    mablo = block_index[gt_x/16][gt_y/16][t_z];
		
		tt = mablo->tiletype[t_x][t_y];
		uint32_t inorg_idx = NOMAT;
		uint32_t grass_idx = NOMAT;
		uint32_t grass_amt = NOMAT;
		uint32_t constr_idx = NOMAT;
		uint32_t stone_idx = lmc.get(mablo->designation[t_x][t_y]);
		for (int i = 0; i < mablo->block_events.size(); i++) {
		    switch (mablo->block_events[i]->getType()) {
			case df::block_square_event_type::mineral:
			{
			   df::block_square_event_mineralst * e = (df::block_square_event_mineralst *)mablo->block_events[i];
			    if ( e->tile_bitmask[t_y] & ( 1 << t_x  ) )
				inorg_idx = e->inorganic_mat;
			    break;
			}
			case df::block_square_event_type::world_construction:
			{
			    df::block_square_event_world_constructionst *e = (df::block_square_event_world_constructionst *)mablo->block_events[i];
			    if ( e->tile_bitmask[t_y] & ( 1 << t_x  ) )
				inorg_idx = constr_idx = e->inorganic_mat;
			    break;
			}
			case df::block_square_event_type::grass:
			{
			    df::block_square_event_grassst *e = (df::block_square_event_grassst *)mablo->block_events[i];
			    grass_idx = e->plant_index;
			    grass_amt = e->amount[t_x][t_y];
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
		}
		fprintf(fp, "%x %x %x %x %x %x %x %x\n", gt_x, gt_y, z, tt, stone_idx, inorg_idx, grass_idx, grass_amt);
	    }
	fclose(fp);
}

