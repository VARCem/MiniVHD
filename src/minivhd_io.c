/**
 * \file
 * \brief Sector reading and writing implementations
 */

#include <stdlib.h>
#include <string.h>
#include "bswap.h"
#include "minivhd_internal.h"
#include "minivhd_util.h"

/* The following bit array macros adapted from 
   http://www.mathcs.emory.edu/~cheung/Courses/255/Syllabus/1-C-intro/bit-array.html */

#define VHD_SETBIT(A,k)     ( A[(k/8)] |= (0x80 >> (k%8)) )
#define VHD_CLEARBIT(A,k)   ( A[(k/8)] &= ~(0x80 >> (k%8)) )
#define VHD_TESTBIT(A,k)    ( A[(k/8)] & (0x80 >> (k%8)) )

static inline void mvhd_check_sectors(int offset, int num_sectors, int total_sectors, int* transfer_sect, int* trunc_sect);
static void mvhd_write_empty_sectors(FILE* f, int sector_count);
static void mvhd_read_sect_bitmap(MVHDMeta* vhdm, int blk);
static void mvhd_write_bat_entry(MVHDMeta* vhdm, int blk);
static void mvhd_create_block(MVHDMeta* vhdm, int blk);
static void mvhd_write_curr_sect_bitmap(MVHDMeta* vhdm);

/**
 * \brief Check that we will not be overflowing buffers
 * 
 * \param [in] offset The offset from which we are beginning from
 * \param [in] num_sectors The number of sectors which we desire to read/write
 * \param [in] total_sectors The total number of sectors available
 * \param [out] transfer_sect The number of sectors to actually write. 
 * This may be lower than num_sectors if offset + num_sectors >= total_sectors
 * \param [out] trunc_sectors The number of sectors truncated if transfer_sectors < num_sectors
 */
static inline void mvhd_check_sectors(int offset, int num_sectors, int total_sectors, int* transfer_sect, int* trunc_sect) {
    *transfer_sect = num_sectors;
    *trunc_sect = 0;
    if ((total_sectors - offset) < *transfer_sect) {
        *transfer_sect = total_sectors - offset;
        *trunc_sect = num_sectors - *transfer_sect;
    }
}

/**
 * \brief Write zero filled sectors to file.
 * 
 * Note, the caller should set the file position before calling this 
 * function for correct operation.
 * 
 * \param [in] f File to write sectors to
 * \param [in] sector_count The number of sectors to write
 */
static void mvhd_write_empty_sectors(FILE* f, int sector_count) {
    uint8_t zero_bytes[MVHD_SECTOR_SIZE] = {0};
    for (int i = 0; i < sector_count; i++) {
        fwrite(zero_bytes, sizeof zero_bytes, 1, f);
    }
}

/**
 * \brief Read the sector bitmap for a block.
 * 
 * If the block is sparse, the sector bitmap in memory will be 
 * zeroed. Otherwise, the sector bitmap is read from the VHD file.
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] blk The block for which to read the sector bitmap from
 */
static void mvhd_read_sect_bitmap(MVHDMeta* vhdm, int blk) {
    if (vhdm->block_offset[blk] != MVHD_SPARSE_BLK) {
        fseeko64(vhdm->f, vhdm->block_offset[blk] * MVHD_SECTOR_SIZE, SEEK_SET);
        fread(vhdm->bitmap.curr_bitmap, vhdm->bitmap.sector_count * MVHD_SECTOR_SIZE, 1, vhdm->f);
    } else {
        memset(vhdm->bitmap.curr_bitmap, 0, vhdm->bitmap.sector_count * MVHD_SECTOR_SIZE);
    }
    vhdm->bitmap.curr_block = blk;
}

/**
 * \brief Write the current sector bitmap in memory to file
 * 
 * \param [in] vhdm MiniVHD data structure
 */
static void mvhd_write_curr_sect_bitmap(MVHDMeta* vhdm) {
    if (vhdm->bitmap.curr_block >= 0) {
        int64_t abs_offset = vhdm->block_offset[vhdm->bitmap.curr_block] * MVHD_SECTOR_SIZE;
        fseeko64(vhdm->f, abs_offset, SEEK_SET);
        fwrite(vhdm->bitmap.curr_bitmap, MVHD_SECTOR_SIZE, vhdm->bitmap.sector_count, vhdm->f);
    }
}

/**
 * \brief Write block offset from memory into file
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] blk The block for which to write the offset for
 */
static void mvhd_write_bat_entry(MVHDMeta* vhdm, int blk) {
    uint64_t table_offset = vhdm->sparse.bat_offset + (blk * sizeof *vhdm->block_offset);
    uint32_t offset = cpu_to_be32(vhdm->block_offset[blk]);
    fseeko64(vhdm->f, table_offset, SEEK_SET);
    fwrite(&offset, sizeof offset, 1, vhdm->f);
}

/**
 * \brief Create an empty block in a sparse or differencing VHD image
 * 
 * VHD images store data in blocks, which are typically 4096 sectors in size 
 * (~2MB). These blocks may be stored on disk in any order. Blocks are created 
 * on demand when required.
 * 
 * This function creates new, empty blocks, by replacing the footer at the end of the file 
 * and then re-inserting the footer at the new file end. The BAT table entry for the
 * new block is updated with the new offset.
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] blk The block number to create
 */
static void mvhd_create_block(MVHDMeta* vhdm, int blk) {
    uint8_t footer[MVHD_FOOTER_SIZE];
    /* Seek to where the footer SHOULD be */
    fseeko64(vhdm->f, -MVHD_FOOTER_SIZE, SEEK_END);
    fread(footer, sizeof footer, 1, vhdm->f);
    fseeko64(vhdm->f, -MVHD_FOOTER_SIZE, SEEK_END);
    if (!mvhd_is_conectix_str(footer)) {
        /* Oh dear. We use the header instead, since something has gone wrong at the footer */
        fseeko64(vhdm->f, 0, SEEK_SET);
        fread(footer, sizeof footer, 1, vhdm->f);
        fseeko64(vhdm->f, 0, SEEK_END);
    }
    int64_t abs_offset = ftello64(vhdm->f);
    if (abs_offset % MVHD_SECTOR_SIZE != 0) {
        /* Yikes! We're supposed to be on a sector boundary. Add some padding */
        int64_t padding_amount = (int64_t)MVHD_SECTOR_SIZE - (abs_offset % MVHD_SECTOR_SIZE);
        uint8_t zero_byte = 0;
        for (int i = 0; i < padding_amount; i++) {
            fwrite(&zero_byte, sizeof zero_byte, 1, vhdm->f);
        }
        abs_offset += padding_amount;
    }
    int sect_offset = (int)(abs_offset / MVHD_SECTOR_SIZE);
    int blk_size_sectors = vhdm->sparse.block_sz / MVHD_SECTOR_SIZE;
    mvhd_write_empty_sectors(vhdm->f, vhdm->bitmap.sector_count + blk_size_sectors);
    /* Add a bit of padding. That's what Windows appears to do, although it's not strictly necessary... */
    mvhd_write_empty_sectors(vhdm->f, 5);
    /* And we finish with the footer */
    fwrite(footer, sizeof footer, 1, vhdm->f);
    /* We no longer have a sparse block. Update that BAT! */
    vhdm->block_offset[blk] = sect_offset;
    mvhd_write_bat_entry(vhdm, blk);
}

/**
 * \brief Read a fixed VHD image
 * 
 * Fixed VHD images are essentially raw image files with a footer tacked on 
 * the end. They are therefore straightforward to write
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_fixed_read(MVHDMeta* vhdm, int offset, int num_sectors, void* out_buff) {
    int64_t addr;
    int transfer_sectors, truncated_sectors;
    int total_sectors = vhdm->footer.geom.cyl * vhdm->footer.geom.heads * vhdm->footer.geom.spt;
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    addr = (int64_t)offset * MVHD_SECTOR_SIZE;
    fseeko64(vhdm->f, addr, SEEK_SET);
    fread(out_buff, transfer_sectors*MVHD_SECTOR_SIZE, 1, vhdm->f);
    return truncated_sectors;
}

/**
 * \brief Read a sparse VHD image
 * 
 * Sparse, or dynamic images are VHD images that grow as data is written to them. 
 * 
 * This function implements the logic to read sectors from the file, taking into 
 * account the fact that blocks may be stored on disk in any order, and that the 
 * read could cross block boundaries.
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_sparse_read(MVHDMeta* vhdm, int offset, int num_sectors, void* out_buff) {
    int transfer_sectors, truncated_sectors;
    int total_sectors = vhdm->footer.geom.cyl * vhdm->footer.geom.heads * vhdm->footer.geom.spt;
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    uint8_t* buff = (uint8_t*)out_buff;
    int64_t addr;
    int s, ls, blk, prev_blk, sib;
    ls = offset + transfer_sectors;
    prev_blk = -1;
    for (s = offset; s < ls; s++) {
        blk = s / vhdm->sect_per_block;
        sib = s % vhdm->sect_per_block;
        if (blk != prev_blk) {
            prev_blk = blk;
            if (vhdm->bitmap.curr_block != blk) {
                mvhd_read_sect_bitmap(vhdm, blk);
                fseeko64(vhdm->f, sib * MVHD_SECTOR_SIZE, SEEK_CUR);
            } else {
                addr = (int64_t)(vhdm->block_offset[blk] + vhdm->bitmap.sector_count + sib) * MVHD_SECTOR_SIZE;
                fseeko64(vhdm->f, addr, SEEK_SET);
            }
        }
        if (VHD_TESTBIT(vhdm->bitmap.curr_bitmap, sib)) {
            fread(buff, MVHD_SECTOR_SIZE, 1, vhdm->f);
        } else {
            memset(buff, 0, MVHD_SECTOR_SIZE);
        }
        buff += MVHD_SECTOR_SIZE;
    }
    return truncated_sectors;
}

/**
 * \brief Read a differencing VHD image
 * 
 * Differencing images are a variant of a sparse image. They contain the grow-on-demand 
 * properties of sparse images, but also reference a parent image. Data is read from the 
 * child image only if it is newer than the data stored in the parent image.
 * 
 * This function implements the logic to read sectors from the child, or a parent image. 
 * Differencing images may have a differencing image as a parent, creating a chain of images. 
 * There is no theoretical chain length limit, although I do not consider long chains to be 
 * advisable. Verifying the parent-child relationship is not very robust.
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_diff_read(MVHDMeta* vhdm, int offset, int num_sectors, void* out_buff) {
    int transfer_sectors, truncated_sectors;
    int total_sectors = vhdm->footer.geom.cyl * vhdm->footer.geom.heads * vhdm->footer.geom.spt;
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    uint8_t* buff = (uint8_t*)out_buff;
    MVHDMeta* curr_vhdm = vhdm;
    int s, ls, blk, sib;
    ls = offset + transfer_sectors;
    for (s = offset; s < ls; s++) {
        while (curr_vhdm->footer.disk_type == MVHD_TYPE_DIFF) {
            blk = s / curr_vhdm->sect_per_block;
            sib = s % curr_vhdm->sect_per_block;
            if (curr_vhdm->bitmap.curr_block != blk) {
                mvhd_read_sect_bitmap(curr_vhdm, blk);
            }
            if (!VHD_TESTBIT(curr_vhdm->bitmap.curr_bitmap, sib)) {
                curr_vhdm = curr_vhdm->parent;
            } else { break; }
        }
        /* We handle actual sector reading using the fixed or sparse functions,
           as a differencing VHD is also a sparse VHD */
        if (curr_vhdm->footer.disk_type == MVHD_TYPE_DIFF || curr_vhdm->footer.disk_type == MVHD_TYPE_DYNAMIC) {
            mvhd_sparse_read(curr_vhdm, s, 1, buff);
        } else {
            mvhd_fixed_read(curr_vhdm, s, 1, buff);
        }
        curr_vhdm = vhdm;
        buff += MVHD_SECTOR_SIZE;
    }
    return truncated_sectors;
}

/**
 * \brief Write to a fixed VHD image
 * 
 * Fixed VHD images are essentially raw image files with a footer tacked on 
 * the end. They are therefore straightforward to write
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_fixed_write(MVHDMeta* vhdm, int offset, int num_sectors, void* in_buff) {
    int64_t addr;
    int transfer_sectors, truncated_sectors;
    int total_sectors = vhdm->footer.geom.cyl * vhdm->footer.geom.heads * vhdm->footer.geom.spt;
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    addr = (int64_t)offset * MVHD_SECTOR_SIZE;
    fseeko64(vhdm->f, addr, SEEK_SET);
    fwrite(in_buff, transfer_sectors*MVHD_SECTOR_SIZE, 1, vhdm->f);
    return truncated_sectors;
}

/**
 * \brief Write to a sparse or differencing VHD image
 * 
 * Sparse, or dynamic images are VHD images that grow as data is written to them. 
 * 
 * Differencing images are a variant of a sparse image. They contain the grow-on-demand 
 * properties of sparse images, but also reference a parent image. Data is always written 
 * to the child image. This makes writing to differencing images essentially identical to 
 * writing to sparse images, hence they use the same function.
 * 
 * This function implements the logic to write sectors to the file, taking into 
 * account the fact that blocks may be stored on disk in any order, and that the 
 * write operation could cross block boundaries.
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_sparse_diff_write(MVHDMeta* vhdm, int offset, int num_sectors, void* in_buff) {
    int transfer_sectors, truncated_sectors;
    int total_sectors = vhdm->footer.geom.cyl * vhdm->footer.geom.heads * vhdm->footer.geom.spt;
    mvhd_check_sectors(offset, num_sectors, total_sectors, &transfer_sectors, &truncated_sectors);
    uint8_t* buff = (uint8_t*)in_buff;
    int64_t addr;
    int s, ls, blk, prev_blk, sib;
    ls = offset + transfer_sectors;
    prev_blk = -1;
    for (s = offset; s < ls; s++) {
        blk = s / vhdm->sect_per_block;
        sib = s % vhdm->sect_per_block;
        if (vhdm->block_offset[blk] == MVHD_SPARSE_BLK) {
            /* "read" the sector bitmap first, before creating a new block, as the bitmap will be
               zero either way */
            mvhd_read_sect_bitmap(vhdm, blk);
            mvhd_create_block(vhdm, blk);
        } 
        if (blk != prev_blk) {
            if (vhdm->bitmap.curr_block != blk) {
                if (prev_blk >= 0) {
                    /* Write the sector bitmap for the previous block, before we replace it. */
                    mvhd_write_curr_sect_bitmap(vhdm);
                }
                mvhd_read_sect_bitmap(vhdm, blk);
                fseeko64(vhdm->f, sib * MVHD_SECTOR_SIZE, SEEK_CUR);
            } else {
                addr = (int64_t)(vhdm->block_offset[blk] + vhdm->bitmap.sector_count + sib) * MVHD_SECTOR_SIZE;
                fseeko64(vhdm->f, addr, SEEK_SET);
            }
            prev_blk = blk;
        }
        fwrite(buff, MVHD_SECTOR_SIZE, 1, vhdm->f);
        VHD_SETBIT(vhdm->bitmap.curr_bitmap, sib);
        buff += MVHD_SECTOR_SIZE;
    }
    /* And write the sector bitmap for the last block we visited to disk */
    mvhd_write_curr_sect_bitmap(vhdm);
    return truncated_sectors;
}

/**
 * \brief A no-op function to "write" to read-only VHD images
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_noop_write(MVHDMeta* vhdm, int offset, int num_sectors, void* in_buff) {
    return 0;
}