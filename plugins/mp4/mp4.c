/*****************************************************************************
 * mp4.c : MP4 file input module for vlc
 *****************************************************************************
 * Copyright (C) 2001 VideoLAN
 * $Id: mp4.c,v 1.1 2002/07/17 21:37:27 fenrir Exp $
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>                                      /* malloc(), free() */
#include <string.h>                                              /* strdup() */
#include <errno.h>
#include <sys/types.h>

#include <vlc/vlc.h>
#include <vlc/input.h>

#include "libmp4.h"
#include "mp4.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static void input_getfunctions( function_list_t * );
static int  MP4Demux         ( input_thread_t * );
static int  MP4Init          ( input_thread_t * );
static void MP4End           ( input_thread_t * );

/*****************************************************************************
 * Build configuration tree.
 *****************************************************************************/
MODULE_CONFIG_START
MODULE_CONFIG_STOP

MODULE_INIT_START
    SET_DESCRIPTION( "MP4 file input" )
    ADD_CAPABILITY( DEMUX, 242 )
MODULE_INIT_STOP

MODULE_ACTIVATE_START
    input_getfunctions( &p_module->p_functions->demux );
MODULE_ACTIVATE_STOP

MODULE_DEACTIVATE_START
MODULE_DEACTIVATE_STOP

/*****************************************************************************
 * Functions exported as capabilities. They are declared as static so that
 * we don't pollute the namespace too much.
 *****************************************************************************/
static void input_getfunctions( function_list_t * p_function_list )
{
#define input p_function_list->functions.demux
    input.pf_init             = MP4Init;
    input.pf_end              = MP4End;
    input.pf_demux            = MP4Demux;
    input.pf_rewind           = NULL;
#undef input
}

/*****************************************************************************
 * Declaration of local function 
 *****************************************************************************/
static void MP4_ParseTrack();

static int MP4_CreateChunksIndex();
static int MP4_CreateSamplesIndex();

static void MP4_StartDecoder();
static void MP4_StopDecoder();

static int  MP4_ReadSample();
static int  MP4_DecodeSample();

#define MP4_Set4BytesLE( p, dw ) \
    *((u8*)p) = ( dw&0xff ); \
    *((u8*)p+1) = ( ((dw)>> 8)&0xff ); \
    *((u8*)p+2) = ( ((dw)>>16)&0xff ); \
    *((u8*)p+3) = ( ((dw)>>24)&0xff )

#define MP4_Set2BytesLE( p, dw ) \
    *((u8*)p) = ( (dw)&0xff ); \
    *((u8*)p+1) = ( ((dw)>> 8)&0xff )

    
/*****************************************************************************
 * MP4Init: check file and initializes MP4 structures
 *****************************************************************************/
static int MP4Init( input_thread_t *p_input )
{
    u8  *p_peek;
    u32 i_type;
    
    demux_data_mp4_t *p_demux;
    
    MP4_Box_t *p_moov;    
    MP4_Box_t *p_ftyp;


    MP4_Box_t *p_mvhd;
    MP4_Box_t *p_trak;

    int i;
    /* I need to seek */
    if( !p_input->stream.b_seekable )
    {
        msg_Warn( p_input, "MP4 plugin discarded (unseekable)" );
        return( -1 );
            
    } 
    /* Initialize access plug-in structures. */
    if( p_input->i_mtu == 0 )
    {
        /* Improve speed. */
        p_input->i_bufsize = INPUT_DEFAULT_BUFSIZE ;
    }

    /* a little test to see if it could be a mp4 */
    if( input_Peek( p_input, &p_peek, 8 ) < 8 )
    {
        msg_Warn( p_input, "MP4 plugin discarded (cannot peek)" );
        return( -1 );
    }
    i_type = ( p_peek[4] << 24 ) + ( p_peek[5] << 16 ) +
                ( p_peek[6] << 8 ) + ( p_peek[7] );
    switch( i_type )
    {
        case( FOURCC_ftyp ):
        case( FOURCC_moov ):
        case( FOURCC_moof ):
        case( FOURCC_mdat ):
        case( FOURCC_udta ): /* should never match but ... */
        case( FOURCC_free ):
        case( FOURCC_skip ):
        case( FOURCC_wide ): /* not mp4 compliant but ... */
            break;
         default:
            msg_Warn( p_input, "MP4 plugin discarded (not a valid file)" );
            return( -1 );
    }

    /* create our structure that will contains all data */
    if( !( p_input->p_demux_data = 
                p_demux = malloc( sizeof( demux_data_mp4_t ) ) ) )
    {
        msg_Err( p_input, "out of memory" );
        return( -1 );
    }
    memset( p_demux, 0, sizeof( demux_data_mp4_t ) );
    p_input->p_demux_data = p_demux;
       

    /* Now load all boxes ( except raw data ) */
    if( !MP4_ReadBoxRoot( p_input, &p_demux->box_root ) )
    {
        msg_Warn( p_input, "MP4 plugin discarded (not a valid file)" );
        return( -1 );
    }

    MP4_DumpBoxStructure( p_input, &p_demux->box_root );

    if( ( p_ftyp = MP4_FindBox( &p_demux->box_root, FOURCC_ftyp ) ) )
    {
        switch( p_ftyp->data.p_ftyp->i_major_brand )
        {
            case( FOURCC_isom ):
                msg_Info( p_input, 
                          "ISO Media file (isom) version %d.",
                          p_ftyp->data.p_ftyp->i_minor_version );
                break;
            default:
                msg_Info( p_input,
                          "Unrecognize major file specification." );
                break;
        }
    }
    else
    {
        msg_Info( p_input, "File Type box missing(assume ISO Media file)" );
    }

    /* the file need to have one moov box */
    if( !( p_moov = MP4_FindBox( &p_demux->box_root, FOURCC_moov ) ) )
    {
        msg_Warn( p_input, "MP4 plugin discarded (missing moov box)" );
        MP4End( p_input );
        return( -1 );
    }

    if( MP4_CountBox( &p_demux->box_root, FOURCC_moov ) != 1 )
    {
        msg_Warn( p_input, "more than one \"moov\" box (continuying anyway)" );
    }

    if( !(p_mvhd = MP4_FindBox( p_moov, FOURCC_mvhd ) ) )
    {
        msg_Err( p_input, "cannot find \"mvhd\" box" );
        MP4End( p_input );
        return( -1 );
    }
    else
    {
        p_demux->i_timescale = p_mvhd->data.p_mvhd->i_timescale;
    }
    
    p_demux->i_tracks = MP4_CountBox( p_moov, FOURCC_trak );
    msg_Dbg( p_input, "find %d track%c",
                        p_demux->i_tracks,
                        p_demux->i_tracks ? 's':' ' );

    if( !( p_trak = MP4_FindBox( p_moov, FOURCC_trak ) ) )
    {
        msg_Err( p_input, "cannot find /moov/trak !" );
        MP4End( p_input );
        return( -1 );
    }

    /* allocate memory */
    p_demux->track = calloc( p_demux->i_tracks, sizeof( track_data_mp4_t ) );

    /* now process each track and extract all usefull informations */
    for( i = 0; i < p_demux->i_tracks; i++ )
    {
        MP4_ParseTrack( p_input, &p_demux->track[i], p_trak );

        if( p_demux->track[i].b_ok )
        {
            char *psz_cat;
            switch( p_demux->track[i].i_cat )
            {
                case( VIDEO_ES ):
                    psz_cat = "video";
                    break;
                case( AUDIO_ES ):
                    psz_cat = "audio";
                    break;
                default:
                    psz_cat = "";
                    break;
            }
            
            msg_Dbg( p_input, "adding track(%d) %s (%s) language %c%c%c",
                            i,
                            psz_cat,
                            p_demux->track[i].b_enable ? "enable":"disable",
                            p_demux->track[i].i_language[0],
                            p_demux->track[i].i_language[1], 
                            p_demux->track[i].i_language[2] );
        }
        else
        {
            msg_Dbg( p_input, "ignoring track(%d)", i );
        }

        p_trak = MP4_FindNextBox( p_trak );
    }
  
    /*  create one program */
    vlc_mutex_lock( &p_input->stream.stream_lock );
    if( input_InitStream( p_input, 0 ) == -1)
    {
        vlc_mutex_unlock( &p_input->stream.stream_lock );
        msg_Err( p_input, "cannot init stream" );
        MP4End( p_input );
        return( -1 );
    }
    if( input_AddProgram( p_input, 0, 0) == NULL )
    {
        vlc_mutex_unlock( &p_input->stream.stream_lock );
        msg_Err( p_input, "cannot add program" );
        MP4End( p_input );
        return( -1 );
    }
    p_input->stream.p_selected_program = p_input->stream.pp_programs[0];
    p_input->stream.i_mux_rate = 0 ; /* FIXME */
    vlc_mutex_unlock( &p_input->stream.stream_lock );
   
    
    for( i = 0; i < p_demux->i_tracks; i++ )
    {
        /* start decoder for this track if enable by default*/
        if( p_demux->track[i].b_enable )
        {
            MP4_StartDecoder( p_input, &p_demux->track[i] );
        }
    }

    vlc_mutex_lock( &p_input->stream.stream_lock );
    p_input->stream.p_selected_program->b_is_ok = 1;
    vlc_mutex_unlock( &p_input->stream.stream_lock );
        
    return( 0 );    

}

/*****************************************************************************
 * MP4Demux: read packet and send them to decoders 
 *****************************************************************************/
static int MP4Demux( input_thread_t *p_input )
{
    demux_data_mp4_t *p_demux = p_input->p_demux_data;
    int i_track;

    /* first wait for the good time to read a packet */

    input_ClockManageRef( p_input,
                          p_input->stream.p_selected_program,
                          p_demux->i_pcr );


    /* update pcr XXX in mpeg scale so in 90000 unit/s */
    p_demux->i_pcr = MP4_GetMoviePTS( p_demux ) * 9 / 100;
    

    /* we will read 100ms for each stream so ...*/
    p_demux->i_time += __MAX( p_demux->i_timescale / 10 , 1 );
    

    for( i_track = 0; i_track < p_demux->i_tracks; i_track++ )
    {
        if( ( !p_demux->track[i_track].b_ok )||
            ( !p_demux->track[i_track].p_es )||
            ( !p_demux->track[i_track].p_es->p_decoder_fifo )||
            ( MP4_GetTrackPTS( &p_demux->track[i_track] ) >=
                        MP4_GetMoviePTS( p_demux ) ) )
        {
            continue; /* no need to read something */
        }

        while( MP4_GetTrackPTS( &p_demux->track[i_track] ) <
                        MP4_GetMoviePTS( p_demux ) )
        {

            pes_packet_t *p_pes;

            /* read a sample */
            if( !MP4_ReadSample( p_input ,
                                 &p_demux->track[i_track],
                                 &p_pes ) )
            {
                break;
            }

            /* send it to decoder and update time of this track 
                 it also launch a new decoder if needed */
            MP4_DecodeSample( p_input ,
                              &p_demux->track[i_track],
                              p_pes );
        }

    }
    
    /* now check if all tracks are finished or unhandled*/
    
    for( i_track = 0; i_track < p_demux->i_tracks; i_track++ )
    {
        if( ( p_demux->track[i_track].b_ok )&&
            ( p_demux->track[i_track].i_sample < p_demux->track[i_track].i_sample_count )&&
            ( p_demux->track[i_track].p_es )&&
            ( p_demux->track[i_track].p_es->p_decoder_fifo ) )
        {
            return( 1 );
        }
    }

    return( 0 ); /* EOF */
}

/*****************************************************************************
 * MP4End: frees unused data
 *****************************************************************************/
static void MP4End( input_thread_t *p_input )
{   
#define FREE( p ) \
    if( p ) { free( p ); } 
    int i_track;
    demux_data_mp4_t *p_demux = p_input->p_demux_data;
    
    msg_Dbg( p_input, "Freeing all memory" );
    MP4_FreeBox( p_input, &p_demux->box_root );
    for( i_track = 0; i_track < p_demux->i_tracks; i_track++ )
    {
        int i_chunk;
        for( i_chunk = 0; 
                i_chunk < p_demux->track[i_track].i_chunk_count; i_chunk++ )
        {
            if( p_demux->track[i_track].chunk )
            {
               FREE(p_demux->track[i_track].chunk[i_chunk].p_sample_count_dts);
               FREE(p_demux->track[i_track].chunk[i_chunk].p_sample_delta_dts );
            }
        }
        if( p_demux->track->p_data_init )
        {
            input_DeletePacket( p_input->p_method_data, 
                                p_demux->track->p_data_init );
        }
        if( !p_demux->track[i_track].i_sample_size )
        {
            FREE( p_demux->track[i_track].p_sample_size );
        }
    }
    FREE( p_demux->track );
#undef FREE
}


/****************************************************************************
 * Local functions, specific to vlc
 ****************************************************************************/

/****************************************************************************
 * Parse track information and create all needed data to run a track
 * If it succeed b_ok is set to 1 else to 0
 ****************************************************************************/
static void MP4_ParseTrack( input_thread_t *p_input,
                     track_data_mp4_t *p_demux_track,
                     MP4_Box_t  * p_trak )
{
    int i;

    MP4_Box_t *p_tkhd = MP4_FindBox( p_trak, FOURCC_tkhd );
    MP4_Box_t *p_tref = MP4_FindBox( p_trak, FOURCC_tref );
    MP4_Box_t *p_edts = MP4_FindBox( p_trak, FOURCC_edts );
    MP4_Box_t *p_mdia = MP4_FindBox( p_trak, FOURCC_mdia );

    MP4_Box_t *p_mdhd;
    MP4_Box_t *p_hdlr;
    MP4_Box_t *p_minf;

    MP4_Box_t *p_vmhd;
    MP4_Box_t *p_smhd; 

    /* hint track unsuported */

    /* by default, track isn't usable */
    p_demux_track->b_ok = 0;

    /* by default, we don't known the categorie */
    p_demux_track->i_cat = UNKNOWN_ES;
    
    if( ( !p_tkhd )||( !p_mdia ) )
    {
        return;
    }

    /* do we launch this track by default ? */
    p_demux_track->b_enable = ( ( p_tkhd->data.p_tkhd->i_flags&MP4_TRACK_ENABLED ) != 0 );

    p_demux_track->i_track_ID = p_tkhd->data.p_tkhd->i_track_ID;
    p_demux_track->i_width = p_tkhd->data.p_tkhd->i_width / 65536;
    p_demux_track->i_height = p_tkhd->data.p_tkhd->i_height / 65536;
    
    if( !p_edts )
    {
//        msg_Warn( p_input, "Unhandled box: edts --> FIXME" );
    }

    if( !p_tref )
    {
//        msg_Warn( p_input, "Unhandled box: tref --> FIXME" );
    } 

    p_mdhd = MP4_FindBox( p_mdia, FOURCC_mdhd );
    p_hdlr = MP4_FindBox( p_mdia, FOURCC_hdlr );
    p_minf = MP4_FindBox( p_mdia, FOURCC_minf );
    
    if( ( !p_mdhd )||( !p_hdlr )||( !p_minf ) )
    {
        return;
    }

    p_demux_track->i_timescale = p_mdhd->data.p_mdhd->i_timescale;

    for( i = 0; i < 3; i++ ) 
    {
        p_demux_track->i_language[i] = p_mdhd->data.p_mdhd->i_language[i];
    }
    
    switch( p_hdlr->data.p_hdlr->i_handler_type )
    {
        case( FOURCC_soun ):
            if( !( p_smhd = MP4_FindBox( p_minf, FOURCC_smhd ) ) )
            {
                return;
            }
            p_demux_track->i_cat = AUDIO_ES;
            break;

        case( FOURCC_vide ):
            if( !( p_vmhd = MP4_FindBox( p_minf, FOURCC_vmhd ) ) )
            {
                return;
            }
            p_demux_track->i_cat = VIDEO_ES;
            break;
            
        default:
            return;
    }
/*  FIXME
    add support to:
    p_dinf = MP4_FindBox( p_minf, FOURCC_dinf );
*/
    if( !( p_demux_track->p_stbl = MP4_FindBox( p_minf, FOURCC_stbl ) ) )
    {
        return;
    }
    
    if( !( p_demux_track->p_stsd = MP4_FindBox( p_demux_track->p_stbl, FOURCC_stsd ) ) )
    {
        return;
    }
    
    /* Create chunk  index table */
    if( !MP4_CreateChunksIndex( p_input,p_demux_track  ) )
    {
        return; /* cannot create chunks index */
    }
    
    /* create sample index table needed for reading and seeking */
    if( !MP4_CreateSamplesIndex( p_input, p_demux_track ) )
    {
        return; /* cannot create samples index */
    }
     
    p_demux_track->b_ok = 1;        
}
                     


/* now create basic chunk data, the rest will be filled by MP4_CreateSamplesIndex */
static int MP4_CreateChunksIndex( input_thread_t *p_input,
                                   track_data_mp4_t *p_demux_track )
{
    MP4_Box_t *p_co64; /* give offset for each chunk, same for stco and co64 */
    MP4_Box_t *p_stsc;

    int i_chunk;
    int i_index, i_last;
   

    if( ( !(p_co64 = MP4_FindBox( p_demux_track->p_stbl, FOURCC_stco ) )&&
                 !(p_co64 = MP4_FindBox( p_demux_track->p_stbl, FOURCC_co64 ) ) )|| 
        ( !(p_stsc = MP4_FindBox( p_demux_track->p_stbl, FOURCC_stsc ) ) ))
    {
        return( 0 );
    }
     
    p_demux_track->i_chunk_count = p_co64->data.p_co64->i_entry_count;
    if( !p_demux_track->i_chunk_count )
    {
        msg_Warn( p_input, "No chunk defined" );
        return( 0 );
    }
    p_demux_track->chunk = calloc( p_demux_track->i_chunk_count, 
                                   sizeof( chunk_data_mp4_t ) );

    /* first we read chunk offset */
    for( i_chunk = 0; i_chunk < p_demux_track->i_chunk_count; i_chunk++ )
    {
        p_demux_track->chunk[i_chunk].i_offset = 
                p_co64->data.p_co64->i_chunk_offset[i_chunk];
    }

    /* now we read index for SampleEntry( soun vide mp4a mp4v ...) 
        to be used for the sample XXX begin to 1 
        We construct it begining at the end */
    i_last = p_demux_track->i_chunk_count; /* last chunk proceded */
    i_index = p_stsc->data.p_stsc->i_entry_count;
    if( !i_index )
    {
        msg_Warn( p_input, "cannot read chunk table or table empty" );
        return( 0 );
    }

    while( i_index )
    {
        i_index--;
        for( i_chunk = p_stsc->data.p_stsc->i_first_chunk[i_index] - 1;
                i_chunk < i_last; i_chunk++ )
        {
            p_demux_track->chunk[i_chunk].i_sample_description_index = 
                    p_stsc->data.p_stsc->i_sample_description_index[i_index];
            p_demux_track->chunk[i_chunk].i_sample_count =
                    p_stsc->data.p_stsc->i_samples_per_chunk[i_index];
        }
        i_last = p_stsc->data.p_stsc->i_first_chunk[i_index] - 1;
    }

    p_demux_track->chunk[i_chunk].i_sample_first = 0;
    for( i_chunk = 1; i_chunk < p_demux_track->i_chunk_count; i_chunk++ )
    {
        p_demux_track->chunk[i_chunk].i_sample_first =
            p_demux_track->chunk[i_chunk-1].i_sample_first + 
                p_demux_track->chunk[i_chunk-1].i_sample_count;
        
    }
    
    msg_Dbg( p_input, "read %d chunk", p_demux_track->i_chunk_count );
    return( 1 );

}



static int MP4_CreateSamplesIndex( input_thread_t *p_input,
                                   track_data_mp4_t *p_demux_track )
{
    MP4_Box_t *p_stts; /* makes mapping between sample and decoding time,
                          ctts make same mapping but for composition time, 
                          not yet used and probably not usefull */
    MP4_Box_t *p_stsz; /* gives sample size of each samples, there is also stz2 
                          that uses a compressed form FIXME make them in libmp4 
                          as a unique type */
    /* TODO use also stss and stsh table for seeking */
    /* FIXME use edit table */
    int i_sample;
    int i_chunk;

    int i_index;
    int i_index_sample_used;

    u64 i_last_dts; 
    
    p_stts = MP4_FindBox( p_demux_track->p_stbl, FOURCC_stts );
    p_stsz = MP4_FindBox( p_demux_track->p_stbl, FOURCC_stsz ); /* FIXME and stz2 */

    
    if( ( !p_stts )||( !p_stsz ) )
    {
        msg_Warn( p_input, "cannot read sample table" );
        return( 0 ); 
    }
        
    p_demux_track->i_sample_count = p_stsz->data.p_stsz->i_sample_count;


    /* for sample size, there are 2 case */
    if( p_stsz->data.p_stsz->i_sample_size )
    {
        /* 1: all sample have the same size, so no need to construct a table */
        p_demux_track->i_sample_size = p_stsz->data.p_stsz->i_sample_size;
        p_demux_track->p_sample_size = NULL;
    }
    else
    {
        /* 2: each sample can have a different size */
        p_demux_track->i_sample_size = 0;
        p_demux_track->p_sample_size = 
            calloc( p_demux_track->i_sample_count, sizeof( u32 ) );
        
        for( i_sample = 0; i_sample < p_demux_track->i_sample_count; i_sample++ )
        {
            p_demux_track->p_sample_size[i_sample] = 
                    p_stsz->data.p_stsz->i_entry_size[i_sample];
        }
    }
    /* we have extract all information from stsz,
        now use stts */

    /* if we don't want to waste too much memory, we can't expand
       the box !, so each chunk will contain an "extract" of this table 
       for fast research */
        
    i_last_dts = 0;
    i_index = 0; i_index_sample_used =0;
    /* create and init last data for each chunk */
    for(i_chunk = 0 ; i_chunk < p_demux_track->i_chunk_count; i_chunk++ )
    {

        int i_entry, i_sample_count, i;
        /* save last dts */
        p_demux_track->chunk[i_chunk].i_first_dts = i_last_dts;
    /* count how many entries needed for this chunk 
       for p_sample_delta_dts and p_sample_count_dts */

        i_entry = 0;
        i_sample_count = p_demux_track->chunk[i_chunk].i_sample_count;
        while( i_sample_count > 0 )
        {
            i_sample_count -= p_stts->data.p_stts->i_sample_count[i_index+i_entry];
            if( i_entry == 0 )
            {
                i_sample_count += i_index_sample_used; /* don't count already used sample 
                                                   int this entry */
            }
            i_entry++;
        }
        /* allocate them */
        p_demux_track->chunk[i_chunk].p_sample_count_dts = 
            calloc( i_entry, sizeof( u32 ) );
        p_demux_track->chunk[i_chunk].p_sample_delta_dts =
            calloc( i_entry, sizeof( u32 ) );

        /* now copy */
        i_sample_count = p_demux_track->chunk[i_chunk].i_sample_count;
        for( i = 0; i < i_entry; i++ )
        {
            int i_used;
            int i_rest;
            
            i_rest = p_stts->data.p_stts->i_sample_count[i_index] - i_index_sample_used;

            i_used = __MIN( i_rest, i_sample_count );

            i_index_sample_used += i_used;

            p_demux_track->chunk[i_chunk].p_sample_count_dts[i] = i_used;

            p_demux_track->chunk[i_chunk].p_sample_delta_dts[i] =
                        p_stts->data.p_stts->i_sample_delta[i_index];
            
            i_last_dts += i_used * 
                    p_demux_track->chunk[i_chunk].p_sample_delta_dts[i];

            if( i_index_sample_used >=
                             p_stts->data.p_stts->i_sample_count[i_index] )
            {
                i_index++;
                i_index_sample_used = 0;
            }
        }
        
    }

    msg_Dbg( p_input, "read %d samples", p_demux_track->i_sample_count );

    return( 1 );
}

static void MP4_StartDecoder( input_thread_t *p_input,
                                 track_data_mp4_t *p_demux_track )
{
    MP4_Box_t *p_sample;
    int i;
    int i_chunk;
    u8  *p_bmih;
 
    int i_codec;
    char *psz_name;
    
    MP4_Box_t *p_esds;

    
    if( (!p_demux_track->b_ok )||( p_demux_track->i_cat == UNKNOWN_ES ) )
    {
        return;
    }
    
    msg_Dbg( p_input, "Starting decoder (track ID 0x%x)",
                      p_demux_track->i_track_ID );

    /* launch decoder according in chunk we are */
    i_chunk = p_demux_track->i_chunk;

    if( !p_demux_track->chunk[i_chunk].i_sample_description_index )
    {
        msg_Warn( p_input, "invalid SampleEntry index for this track i_cat %d" );
        return;
    } 
    
    p_sample = MP4_FindNbBox( p_demux_track->p_stsd,
                              p_demux_track->chunk[i_chunk].i_sample_description_index - 1);

    if( !p_sample )
    {
        msg_Warn( p_input, "cannot find SampleEntry for this track" );
        return;
    }

    
    vlc_mutex_lock( &p_input->stream.stream_lock );
    p_demux_track->p_es = input_AddES( p_input,
                                       p_input->stream.p_selected_program, 
                                       p_demux_track->i_track_ID,
                                       0 );
    vlc_mutex_unlock( &p_input->stream.stream_lock );
    /* Initialise ES, first language as description */
    for( i = 0; i < 3; i++ )
    {
        p_demux_track->p_es->psz_desc[i] = p_demux_track->i_language[i];
    }
    p_demux_track->p_es->psz_desc[4] = 0;
    
    p_demux_track->p_es->i_stream_id = p_demux_track->i_track_ID;

    p_demux_track->p_es->i_type = UNKNOWN_ES;
    p_demux_track->p_es->i_cat = p_demux_track->i_cat;

    /* search for the codec */
    if( !MP4_GetCodec( p_sample->i_type, &i_codec, &psz_name ) )
    {
        msg_Warn( p_input, "%s (%c%c%c%c) unsupported", 
                  psz_name,
                  (p_sample->i_type >> 24)&0xff,
                  (p_sample->i_type >> 16)&0xff,
                  (p_sample->i_type >> 8)&0xff,
                  (p_sample->i_type )&0xff);
        p_demux_track->b_ok = 0;
        return;
    }
    else
    {
        p_demux_track->p_es->i_type = i_codec;
        msg_Info( p_input, "%s supported", psz_name );
    }

    switch( p_demux_track->i_cat )
    {
        case( VIDEO_ES ):    
            p_demux_track->p_es->b_audio = 0;

            /* now create a bitmapinfoheader_t for decoder */
            p_bmih = malloc( 40 );
            memset( p_bmih, 0, 40);
            MP4_Set4BytesLE( p_bmih, 40 );
            if( p_sample->data.p_sample_mp4v->i_width )
            {
                MP4_Set4BytesLE( p_bmih + 4, p_sample->data.p_sample_mp4v->i_width );
            }
            else
            {
                /* use display size */
                MP4_Set4BytesLE( p_bmih + 4, p_demux_track->i_width );
            }
            if( p_sample->data.p_sample_mp4v->i_height )
            {
                MP4_Set4BytesLE( p_bmih + 8, p_sample->data.p_sample_mp4v->i_height );
            }
            else
            {
                MP4_Set4BytesLE( p_bmih + 8, p_demux_track->i_height );
            }

            p_demux_track->p_es->p_demux_data = p_bmih;
            break;
        case( AUDIO_ES ):
            p_demux_track->p_es->b_audio = 1;
            break;
        default:
            break;
    }

    vlc_mutex_lock( &p_input->stream.stream_lock );
    input_SelectES( p_input, p_demux_track->p_es );
    vlc_mutex_unlock( &p_input->stream.stream_lock );
    

    p_demux_track->b_ok = 1;

    /* now see if esds is present and i so create a data packet 
        with decoder_specific_info  */
    if( ( p_esds = MP4_FindBox( p_sample, FOURCC_esds ) )&&
        ( p_esds->data.p_esds->es_descriptor.p_decConfigDescr )&&
        ( p_esds->data.p_esds->es_descriptor.p_decConfigDescr->i_decoder_specific_info_len ) )
    {
        data_packet_t *p_data;
        int i_size = p_esds->data.p_esds->es_descriptor.p_decConfigDescr->i_decoder_specific_info_len;

        /* data packet for the data */
        if( !(p_data = input_NewPacket( p_input->p_method_data, i_size ) ) )
        {
            return;
        }

        /* initialisation of all the field */
        memcpy( p_data->p_payload_start,
                p_esds->data.p_esds->es_descriptor.p_decConfigDescr->p_decoder_specific_info,
                i_size ); 
        p_demux_track->p_data_init = p_data;
    }
                           
    
    return;
}


static void MP4_StopDecoder( input_thread_t *p_input,
                             track_data_mp4_t *p_demux_track )
{
    msg_Dbg( p_input, "Stopping decoder (track ID 0x%x)",
                      p_demux_track->i_track_ID );

    input_UnselectES( p_input, p_demux_track->p_es );
    p_demux_track->p_es = NULL;
    if( p_demux_track->p_data_init )
    {
        input_DeletePacket( p_input->p_method_data, 
                            p_demux_track->p_data_init );
        p_demux_track->p_data_init = NULL;
    }
    
}

static int  MP4_ReadSample( input_thread_t *p_input,
                            track_data_mp4_t *p_demux_track,
                            pes_packet_t **pp_pes )
{
    int i_size;
    off_t i_pos;

    data_packet_t *p_data;


    /* this track have already reach the end */
    if( p_demux_track->i_sample >= p_demux_track->i_sample_count )
    {
        *pp_pes = NULL;
        return( 0 );
    }
    /* caculate size and position for this sample */
    i_size = p_demux_track->i_sample_size ? 
        p_demux_track->i_sample_size : p_demux_track->p_sample_size[p_demux_track->i_sample];
    /* TODO */
    i_pos  = MP4_GetTrackPos( p_demux_track );

    /* go,go go ! */
    if( ! MP4_SeekAbsolute( p_input, i_pos ) )
    {
        return( 0 );
    }

    /* now create a pes */
    if( !(*pp_pes = input_NewPES( p_input->p_method_data ) ) )
    {
        return( 0 );
    }
    /* and a data packet for the data */
    if( !(p_data = input_NewPacket( p_input->p_method_data, i_size ) ) )
    {
        input_DeletePES( p_input->p_method_data, *pp_pes );
        *pp_pes = NULL;
        return( 0 );
    }
    
    /* initialisation of all the field */
    (*pp_pes)->i_dts =
        (*pp_pes)->i_pts = MP4_GetTrackPTS( p_demux_track );
    (*pp_pes)->p_first = (*pp_pes)->p_last  = p_data;
    (*pp_pes)->i_nb_data = 1;
    (*pp_pes)->i_pes_size = i_size;

    if( !i_size )    
    {
        return( 1 );
    }
    
//    msg_Dbg( p_input, "will read %d bytes", i_size );
    if( !MP4_ReadData( p_input, p_data->p_payload_start, i_size ) )
    {
        input_DeletePES( p_input->p_method_data, *pp_pes );
        input_DeletePacket( p_input->p_method_data, p_data );
        return( 0 );
    }

	return( 1 );
}


static int  MP4_DecodeSample( input_thread_t *p_input,
                              track_data_mp4_t *p_demux_track,
                              pes_packet_t *p_pes )
{

    if( !p_pes )
    {
        return( 0 );
    }

    /* don't forget to convert in mpeg clock */
    /* FIXME correct ffmpeg to use dts instead of pts that it incorrect 
       and, set it here ( and correct avi demux ) */
    p_pes->i_dts =
        p_pes->i_pts = input_ClockGetTS( p_input,
                                         p_input->stream.p_selected_program,
                                         p_pes->i_pts * 9/100);

    
    if( p_demux_track->p_data_init )
    {
        pes_packet_t *p_pes_init;
        /* create a pes packet containing decoder initialisation 
           with the one we will send to decoder */
        if( !(p_pes_init = input_NewPES( p_input->p_method_data ) ) )
        {
            msg_Err( p_input, "out of memory" );
            return( 0 );
        }
        
        p_pes_init->p_first = 
            p_pes_init->p_last = p_demux_track->p_data_init;

        p_pes_init->i_pes_size = p_demux_track->p_data_init->p_payload_end - 
                                   p_demux_track->p_data_init->p_payload_start;
        p_pes_init->i_nb_data = 1;

        input_DecodePES( p_demux_track->p_es->p_decoder_fifo, p_pes_init );
        p_demux_track->p_data_init = NULL;
    }

    input_DecodePES( p_demux_track->p_es->p_decoder_fifo, p_pes );
    
    /* now update sample position */
    p_demux_track->i_sample++; /* easy ;) */
    if( p_demux_track->i_sample >= p_demux_track->i_sample_count )
    {
        /* we have reach end of the track so free decoder stuff */
        MP4_StopDecoder( p_input, p_demux_track );
        return( 1 );
    }
    /* Have we changed chunk ? */
    if( p_demux_track->i_sample >=
            p_demux_track->chunk[p_demux_track->i_chunk].i_sample_first +
                p_demux_track->chunk[p_demux_track->i_chunk].i_sample_count )
    {
        /* we haven't reached the end of the track, so see if we 
           have to change the decoder for the next frame because 
           i_sample_description_index have changed */

        p_demux_track->i_chunk++;
        if( p_demux_track->chunk[p_demux_track->i_chunk-1].i_sample_description_index 
              != p_demux_track->chunk[p_demux_track->i_chunk].i_sample_description_index  )
        {
            /* FIXME */
            msg_Err( p_input, "I need to change the decoder but not yet implemented" );
            return( 0 );
        }
    }

    
    return( 1 );
}



