
/*
Copyright (c) 2015, 2015, Oracle and/or its affiliates. All rights reserved.

The Universal Permissive License (UPL), Version 1.0

Subject to the condition set forth below, permission is hereby granted to any
person obtaining a copy of this software, associated documentation and/or data
(collectively the "Software"), free of charge and under any and all copyright
rights in the Software, and any and all patent rights owned or freely
licensable by each licensor hereunder covering either (i) the unmodified
Software as contributed to or provided by such licensor, or (ii) the Larger
Works (as defined below), to deal in both

(a) the Software, and

(b) any piece of software and/or hardware listed in the lrgrwrks.txt file if
one is included with the Software (each a "Larger Work" to which the Software
is contributed by such licensors),

without restriction, including without limitation the rights to copy, create
derivative works of, display, perform, and distribute the Software and make,
use, sell, offer for sale, import, export, have made, and have sold the
Software and the Larger Work(s), and to sublicense the foregoing rights on
either these or other terms.

This license is subject to the following condition:

The above copyright notice and either this complete permission notice or at a
minimum a reference to the UPL must be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
/**\file
   NAME\n
     nvm_usidmap.c - nvm_type and nvm_extern data for the NVM library

   DESCRIPTION\n
     This file would normally be generated by the USID map utility. Until it 
     exists, this is a hand crafted version of the USID const data for the
     NVM library. Some day this file will disappear.

 */
#ifdef NVM_EXT
/* generated file usid.c contains the nvm_type and nvm_extern  definitions */
#else
#include "nvm_usid.h"
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[2];
} nvm_type1;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[3];
} nvm_type2;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[4];
} nvm_type3;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[5];
} nvm_type4;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[6];
} nvm_type5;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[7];
} nvm_type6;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[8];
} nvm_type7;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[9];
} nvm_type8;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[17];
} nvm_type16;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[16];
} nvm_type15;
typedef struct {
    const nvm_usid usid;
    const char *const name;
    const char *const tag;
    const size_t size;
    const size_t xsize;
    const uint32_t version;
    const uint32_t align;
    void (*const upgrade)();
    const nvm_type *const uptype;
    const size_t field_cnt;
    const nvm_field field[23];
} nvm_type22;

/* an extern definition for each nvm_type */
extern const nvm_type3 nvm_type_nvm_extent;
extern const nvm_type15 nvm_type_nvm_region;
extern const nvm_type5 nvm_type_nvm_trans_table;
extern const nvm_type16 nvm_type_nvm_heap;
extern const nvm_type5 nvm_type_nvm_amutex;
extern const nvm_type4 nvm_type_nvm_mutex_array;
extern const nvm_type2 nvm_type_nvm_link;
extern const nvm_type5 nvm_type_nvm_blk;
//extern const nvm_type nvm_type_;

/*
 *  type definition of nvm_extent 
 */
const nvm_type3 nvm_type_nvm_extent = {
    { 0x93dd163c10b10691LL, 0xfdf780feccc95b0aLL}, // type_usid
    "nvm_extent", // name
    "One extent of a region file", // tag
    32, // size
    0, // xsize
    0, // version
    16, // align
    0, // upgrade
    0, // uptype
    3, // field_cnt
    { // fields[] 
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_pointer, 0, 1, 0, "addr"},
        {nvm_field_unsigned, 0, 1, (void*)64, "size"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * type definition of nvm_region_header. This is needed because it is
 * a non-persistent struct that appears in a persistent struct.
 */
const nvm_type6 nvm_type_nvm_region_header = {
    {0, 0}, // type_usid
    "nvm_region_header", // name
    0, //tag
    80, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    7, // field_cnt
    { // fields[] 
        {nvm_field_unsigned, 0, 1, (void*)64, "vsize"},
        {nvm_field_unsigned, 0, 1, (void*)64, "psize"},
        {nvm_field_char, 0, 64, (void*)8, "name"},
        {nvm_field_usid, 0, 1, (void*)nvm_usid_struct, "rootUSID"},
        {nvm_field_pointer,  0, 1, NULL, "rootObject"},
        {nvm_field_unsigned, 0, 1, (void*)64, "attach_id"},
        {0, 0, 0, 0, 0} // end
    }
};


/*
 *  type definition of nvm_region 
 */
const nvm_type15 nvm_type_nvm_region = {
    { 0xeeae207b78917578LL, 0xd7aeb6aa1294d9a6LL}, // type_usid
    "nvm_region", // name
    "An NVM Direct managed region begins with an nvm_region", // tag
    1024, // size
    0, // xsize
    0, // version
    16, // align
    0, // upgrade
    0, // uptype
    16, // field_cnt
    { // fields[] 
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_struct, 0, 1, &nvm_type_nvm_region_header, "header"},
        {nvm_field_unsigned, 0, 1, (void*)64, "attach_cnt"},
        {nvm_field_unsigned, 0, 1, (void*)64, "spare"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_extent, "extents"},
        {nvm_field_unsigned, 0, 1, (void*)32, "extents_size"},
        {nvm_field_unsigned, 0, 1, (void*)32, "extent_count"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_trans_table, "nvtt_list"},
        {nvm_field_unsigned, 0, 1, (void*)32, "max_transactions"},
        {nvm_field_unsigned, 0, 1, (void*)32, "max_undo_blocks"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_heap, "rootHeap"},
        {nvm_field_unsigned, 0, 1, (void*)64, "reg_mutex"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_mutex_array,
                "upgrade_mutexes"},
        {nvm_field_unsigned, 1, 1, (void*)32, "desc"},
        {nvm_field_pad, 0, 1024 - 204, (void*)8, "_padding_to_1024"},
        {0, 0, 0, 0, 0} // end
    }
};


/*
 * Type definition of nvm_link
 */
const nvm_type2 nvm_type_nvm_link = {
    {0, 0}, // type_usid
    "nvm_link", // name
    "Link in doubly linked list of nvm_blk", // tag
    16, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    2, // field_cnt
    { // fields[] 
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_blk, "fwrd"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_blk, "back"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_list
 */
const nvm_type2 nvm_type_nvm_list = {
    {0, 0}, // type_usid
    "nvm_list", // name
    "Doubly linked list of nvm_blk", // tag
    16, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    2, // field_cnt
    { // fields[] 
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_blk, "fwrd"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_blk, "back"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_blk
 */
const nvm_type5 nvm_type_nvm_blk = {
    {0x4d8ab944afee7dddLL, 0x06c7ac6648d7e7b3LL}, // type_usid
    "nvm_blk", // name
    "NVM heap control block describing one chunk of NVM", // tag
    64, //size
    0, // xsize
    0, // version
    64, // align
    0, // upgrade
    0, // uptype
    5, // field_cnt
    { // fields[] 
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_struct, 0, 1, &nvm_type_nvm_link, "neighbors"},
        {nvm_field_struct, 0, 1, &nvm_type_nvm_link, "group"},
        {nvm_field_pointer, 0, 1, NULL, "ptr"},
        {nvm_field_unsigned, 0, 1, (void*)64, "allocated"},
        {0, 0, 0, 0, 0} // end
    }
};


/*
 * Type definition of nvm_heap
 */
const nvm_type16 nvm_type_nvm_heap = {
    {0xf72849918dcf67f2LL, 0xc6d48417fe4abdf6LL}, // type_usid
    "nvm_heap", // name
    "Metadata describing one heap or subheap", // tag
    1024, //size
    0, // xsize
    0, // version
    64, // align
    0, // upgrade
    0, // uptype
    16, // field_cnt
    { // fields[] 
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_char, 0, 64, (void*)8, "name"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_region, "region"},
        {nvm_field_pointer, 0, 1, NULL, "extent"},
        {nvm_field_pointer, 0, 1, NULL, "rootobject"},
        {nvm_field_unsigned, 0, 1, (void*)64, "psize"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_blk, "first"},
        {nvm_field_unsigned, 0, 1, (void*)64, "size"},
        {nvm_field_unsigned, 0, 1, (void*)64, "consumed"},
        {nvm_field_unsigned, 0, 1, (void*)64, "heap_mutex"},
        {nvm_field_struct, 0, 1, &nvm_type_nvm_list, "list"},
        {nvm_field_unsigned, 0, 25, (void*)64, "free_size"},
        {nvm_field_struct, 0, 25, &nvm_type_nvm_list, "free"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_blk, "nvb_free"},
        {nvm_field_unsigned, 0, 1, (void*)32, "inuse"},
        {nvm_field_pad, 0, 1024 - 772, (void*)8, "_padding_to_1024"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_undo_blk
 */
const nvm_type7 nvm_type_nvm_undo_blk = {
    {0xfb7ad37ce443f03bLL, 0x0e559be8bd5ea95eLL}, // type_usid
    "nvm_undo_blk", // name
    "One block of undo for a transaction", // tag
    4096, //size
    0, // xsize
    0, // version
    16, // align
    0, // upgrade
    0, // uptype
    7, // field_cnt
    { // fields[] 
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_undo_blk, "link"},
        {nvm_field_unsigned, 0, 1, (void*)16, "count"},
        {nvm_field_unsigned, 0, 1, (void*)16, "hi_count"},
        {nvm_field_unsigned, 0, 1, (void*)32, "txnum"},
        {nvm_field_pad, 0, 32, (void*)8, "_pad1"},
        {nvm_field_unsigned, 0, 4096 - 64, (void*)8, "data"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_restore
 */
const nvm_type2 nvm_type_nvm_restore = {
    {0, 0}, // type_usid
    "nvm_restore", // name
    "Data for undo operation nvm_op_restore", // tag
    8, //size
    1, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    2, // field_cnt
    { // fields[] 
        {nvm_field_pointer, 0, 1, NULL, "addr"},
        {nvm_field_unsigned, 0, 0, (void*)8, "data"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_lkrec
 */
const nvm_type7 nvm_type_nvm_lock = {
    {0, 0}, // type_usid
    "nvm_lkrec", // name
    "Data for undo operation nvm_op_xlock or nvm_op_slock", // tag
    24, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    7, // field_cnt
    { // fields[] 
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_lock, "prev"},
        {nvm_field_unsigned, 0, 1, (void*)32, "undo_ops"},
        {nvm_field_unsigned, 0, 1, (void*)8, "state"},
        {nvm_field_unsigned, 0, 1, (void*)8, "old_level"},
        {nvm_field_unsigned, 0, 1, (void*)8, "new_level"},
        {nvm_field_pad, 0, 1, (void*)8, "_pad1"},
        {nvm_field_unsigned, 0, 1, (void*)64, "mutex"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_savepnt
 */
const nvm_type4 nvm_type_nvm_savepoint = {
    {0, 0}, // type_usid
    "nvm_savepnt", // name
    "Data for undo operation nvm_op_savepoint", // tag
    24, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    4, // field_cnt
    { // fields[] 
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_savepoint, "prev"},
        {nvm_field_unsigned, 0, 1, (void*)32, "undo_ops"},
        {nvm_field_pad, 0, 4, (void*)8, "_pad1"},
        {nvm_field_pointer, 0, 1, NULL, "name"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_nested
 */
const nvm_type4 nvm_type_nvm_nested = {
    {0, 0}, // type_usid
    "nvm_nested", // name
    "Data for undo operation nvm_op_nested", // tag
    16, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    4, // field_cnt
    { // fields[] 
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_nested, "prev"},
        {nvm_field_unsigned, 0, 1, (void*)32, "undo_ops"},
        {nvm_field_signed, 0, 1, (void*)32, "state"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_on_commit
 */
const nvm_type2 nvm_type_nvm_on_abort = {
    {0, 0}, // type_usid
    "nvm_on_abort", // name
    "Data for undo operation nvm_op_on_abort", // tag
    16, //size
    1, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    2, // field_cnt
    { // fields[] 
        {nvm_field_usid, 0, 1, (void*)nvm_usid_func, "func"},
        {nvm_field_unsigned, 0, 0, (void*)8, "data"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_on_commit
 */
const nvm_type6 nvm_type_nvm_on_commit = {
    {0, 0}, // type_usid
    "nvm_on_commit", // name
    "Data for undo operation nvm_op_on_commit", // tag
    32, //size
    1, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    6, // field_cnt
    { // fields[] 
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_on_commit, "prev"},
        {nvm_field_unsigned, 0, 1, (void*)32, "undo_ops"},
        {nvm_field_pad, 0, 4, (void*)8, "_pad1"},
        {nvm_field_struct_ptr, 1, 1, &nvm_type_nvm_on_commit, "next"},
        {nvm_field_usid, 0, 1, (void*)nvm_usid_func, "func"},
        {nvm_field_unsigned, 0, 0, (void*)8, "data"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_transaction
 */
const nvm_type22 nvm_type_nvm_transaction = {
    {0x37ca2aace2f64639, 0x18e6f0152d8c2a6e}, // type_usid
    "nvm_transaction", // name
    "Persistent data describing a transaction", // tag
    128, //size
    0, // xsize
    0, // version
    16, // align
    0, // upgrade
    0, // uptype
    22, // field_cnt
    { // fields[] 
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_signed, 0, 1, (void*)16, "slot"},
        {nvm_field_signed, 0, 1, (void*)8, "state"},
        {nvm_field_signed, 0, 1, (void*)8, "dead"},
        {nvm_field_unsigned, 0, 1, (void*)32, "txnum"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_undo_blk, "undo"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_on_commit, "commit_ops"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_lock, "held_locks"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_savepoint, "savept_last"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_nested, "nstd_last"},
        {nvm_field_unsigned, 0, 1, (void*)32, "max_undo_blocks"},
        {nvm_field_unsigned, 1, 1, (void*)32, "cur_undo_blocks"},
        {nvm_field_unsigned, 1, 1, (void*)32, "desc"},
        {nvm_field_unsigned, 1, 1, (void*)32, "undo_ops"},
        {nvm_field_unsigned, 1, 1, (void*)32, "undo_bytes"},
        {nvm_field_unsigned, 1, 1, (void*)16, "spawn_time"},
        {nvm_field_unsigned, 1, 1, (void*)8, "spawn_cnt"},
        {nvm_field_pad, 0, 1, (void*)8, "_pad1"},
        {nvm_field_struct_ptr, 1, 1, &nvm_type_nvm_transaction, "parent"},
        {nvm_field_pointer, 1, 1, NULL, "undo_data"},
        {nvm_field_struct_ptr, 1, 1, &nvm_type_nvm_transaction, "link"},
        {nvm_field_pad, 0, 128 - 112, (void*)8, "_padding_to_128"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_trans_table
 */
const nvm_type5 nvm_type_nvm_trans_table = {
    {0x1c2be7506f5ff07a, 0x30835fc7b2be8c09}, // type_usid
    "nvm_trans_table", // name
    "A group of transaction slots and undo blocks", // tag
    1024*1024, //size
    0, // xsize
    0, // version
    16, // align
    0, // upgrade
    0, // uptype
    5, // field_cnt
    { // fields[] 
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_trans_table, "link"},
        {nvm_field_pad, 0, 128 - 24, (void*)8, "_pad1"},
        {nvm_field_struct, 0, 63, &nvm_type_nvm_transaction, "transactions"},
        {nvm_field_struct, 0, 254, &nvm_type_nvm_undo_blk, "undo_blks"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_amutex
 */
const nvm_type5 nvm_type_nvm_amutex = {
    {0, 0}, // type_usid
    "nvm_amutex", // name
    "A mutex in NVM which is lockable by an NVM transaction", // tag
    8, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    5, // field_cnt
    { // fields[] 
        {nvm_field_signed, 0, 1, (void*)16, "owners"},
        {nvm_field_unsigned, 0, 1, (void*)16, "x_waiters"},
        {nvm_field_unsigned, 0, 1, (void*)16, "s_waiters"},
        {nvm_field_unsigned, 0, 1, (void*)8, "level"},
        {nvm_field_pad, 0, 1, (void*)8, "_pad1"},
        {0, 0, 0, 0, 0} // end
    }

};

/*
 * Type definition of nvm_mutex_array
 */
const nvm_type4 nvm_type_nvm_mutex_array = {
    {0x334fc14d262da28c, 0xe5e04e9e37509c23}, // type_usid
    "nvm_mutex_array", // name
    "Array of mutexes to lock objects by their address", // tag
    24, //size
    8, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    4, // field_cnt
    { // fields[]
        {nvm_field_usid, 0, 1, (void*)nvm_usid_self, "type_usid"},
        {nvm_field_unsigned, 0, 1, (void*)32, "count"},
        {nvm_field_unsigned, 0, 1, (void*)32, "destroyed"},
        {nvm_field_unsigned, 0, 0, (void*)64, "mutexes"},
        {0, 0, 0, 0, 0} // end
    }

};

/*
 * Type definition of nvm_free_ctx
 */
const nvm_type1 nvm_type_nvm_free_ctx = {
    {0, 0}, // type_usid
    "nvm_free_ctx", // name
    "Context for onabort/oncommit operation to free NVM", // tag
    8, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    1, // field_cnt
    { // fields[] 
        {nvm_field_pointer, 0, 1, NULL, "ptr"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_remx_ctx
 */
const nvm_type3 nvm_type_nvm_remx_ctx = {
    {0, 0}, // type_usid
    "nvm_remx_ctx", // name
    "Context for onabort/oncommit operation to remove NVM from an extent", // tag
    24, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    3, // field_cnt
    { // fields[] 
        {nvm_field_unsigned, 0, 1, (void*)64, "offset"},
        {nvm_field_pointer, 0, 1, NULL, "addr"},
        {nvm_field_unsigned, 0, 1, (void*)64, "size"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_inuse_ctx
 */
const nvm_type1 nvm_type_nvm_inuse_ctx = {
    {0, 0}, // type_usid
    "nvm_inuse_ctx", // name
    "Context for onabort/oncommit operation to clear heap inuse flag", // tag
    8, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    1, // field_cnt
    { // fields[]
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_heap, "heap"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * Type definition of nvm_txconfig_ctx
 */
const nvm_type3 nvm_type_nvm_txconfig_ctx = {
    {0, 0}, // type_usid
    "nvm_txconfig_ctx", // name
    "Context for onunlock operation to reconfigure transaction data", // tag
    16, //size
    0, // xsize
    0, // version
    8, // align
    0, // upgrade
    0, // uptype
    3, // field_cnt
    { // fields[] 
        {nvm_field_struct_ptr, 0, 1, &nvm_type_nvm_trans_table, "tt_append"},
        {nvm_field_unsigned, 0, 1, (void*)32, "txn_slots"},
        {nvm_field_unsigned, 0, 1, (void*)32, "undo_limit"},
        {0, 0, 0, 0, 0} // end
    }
};

/*
 * The following array of pointers to nvm_type structs is used to initialize
 * The USID map. Every nvm_type that contains a USID should be present in this 
 * array.
 */
static const nvm_type *types[] = {
    (const nvm_type*)&nvm_type_nvm_extent,
    (const nvm_type*)&nvm_type_nvm_region,
    (const nvm_type*)&nvm_type_nvm_blk,
    (const nvm_type*)&nvm_type_nvm_heap,
    (const nvm_type*)&nvm_type_nvm_trans_table,
    (const nvm_type*)&nvm_type_nvm_transaction,
    (const nvm_type*)&nvm_type_nvm_undo_blk,
//    &,
    NULL // NULL terminator
};

/*
 * The following are USID values for various external symbols.
 */
//void nvm_free_callback(void *ctx);
extern int nvm_free_callback;
const nvm_extern nvm_extern_nvm_free_callback = {
    {0x3191c3be57cf5408, 0x3492134cdf536df7},
    &nvm_free_callback,
    (const nvm_type*)&nvm_type_nvm_free_ctx,
    "nvm_free_callback"
};
//void nvm_freex_callback(void *ctx);
extern int nvm_freex_callback;
const nvm_extern nvm_extern_nvm_freex_callback = {
    {0x009e1ed41f0d1ea0, 0x9f9e0dbcabf56ae0},
    &nvm_freex_callback,
    (const nvm_type*)&nvm_type_nvm_remx_ctx,
    "nvm_freex_callback"
};
//void nvm_remx_callback(void *ctx);
extern int nvm_remx_callback;
const nvm_extern nvm_extern_nvm_remx_callback = {
    {0x2732fd0f0a1311e2, 0xf304ce0c9fd533f8},
    &nvm_remx_callback,
    (const nvm_type*)&nvm_type_nvm_remx_ctx,
    "nvm_remx_callback"
};
//void nvm_inuse_callback(void *ctx);
extern int nvm_inuse_callback;
const nvm_extern nvm_extern_nvm_inuse_callback = {
    {0x5d8f80344a7e08dd, 0x1f048aa698a5f4c1},
    &nvm_inuse_callback,
    (const nvm_type*)&nvm_type_nvm_inuse_ctx,
    "nvm_inuse_callback"
};
//void nvm_txconfig_callback(void *ctx);
extern int nvm_txconfig_callback;
const nvm_extern nvm_extern_nvm_txconfig_callback = {
    {0x7ac98b1f63f35abb, 0x5a50281f904707e2},
    &nvm_txconfig_callback,
    (const nvm_type*)&nvm_type_nvm_txconfig_ctx,
    "nvm_txconfig_callback"
};

/*
 * The following array of pointers to nvm_exterm structs is used to initialize
 * The USID map. Every nvm_extern that contains a USID should be present in this 
 * array.
 */
static const nvm_extern *externs[] = {
    &nvm_extern_nvm_free_callback,
    &nvm_extern_nvm_freex_callback,
    &nvm_extern_nvm_remx_callback,
    &nvm_extern_nvm_txconfig_callback,
    &nvm_extern_nvm_inuse_callback,
    NULL // NULL terminator
};

/*
 * function to register USID mappings for the NVM library
 * 
 * @return 1 if all mappings saved, 0 if there are redundant mappings that are
 * ignored.,
 */
int nvm_usidmap_register(void)
{
    /* Register the nvm_types for the nvm library */
    int unique = nvm_usid_register_types(types);

    /* Register the functions for on commit and on abort operations */
    unique &= nvm_usid_register_externs(externs);
    
    return unique;
}
#endif //NVM_EXT