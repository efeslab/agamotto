/*
 * Copyright 2015-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * rbtree_map.h -- TreeMap sorted collection implementation
 */

#ifndef RBTREE_MAP_H
#define RBTREE_MAP_H

#include <libpmemobj.h>

#ifndef RBTREE_MAP_TYPE_OFFSET
#define RBTREE_MAP_TYPE_OFFSET 1016
#endif

#define NODE_P(_n)\
D_RW(_n)->parent

#define NODE_GRANDP(_n)\
NODE_P(NODE_P(_n))

#define NODE_PARENT_AT(_n, _rbc)\
D_RW(NODE_P(_n))->slots[_rbc]

#define NODE_PARENT_RIGHT(_n)\
NODE_PARENT_AT(_n, RB_RIGHT)

#define NODE_IS(_n, _rbc)\
TOID_EQUALS(_n, NODE_PARENT_AT(_n, _rbc))

#define NODE_IS_RIGHT(_n)\
TOID_EQUALS(_n, NODE_PARENT_RIGHT(_n))

#define NODE_LOCATION(_n)\
NODE_IS_RIGHT(_n)

#define RB_FIRST(_m)\
D_RW(D_RW(_m)->root)->slots[RB_LEFT]

#define NODE_IS_NULL(_n)\
TOID_EQUALS(_n, s)

struct rbtree_map;
TOID_DECLARE(struct rbtree_map, RBTREE_MAP_TYPE_OFFSET + 0);
TOID_DECLARE(struct tree_map_node, RBTREE_MAP_TYPE_OFFSET + 1);

enum rb_color {
	COLOR_BLACK,
	COLOR_RED,

	MAX_COLOR
};

enum rb_children {
	RB_LEFT,
	RB_RIGHT,

	MAX_RB
};

struct tree_map_node {
	uint64_t key;
	PMEMoid value;
	enum rb_color color;
	TOID(struct tree_map_node) parent;
	TOID(struct tree_map_node) slots[MAX_RB];
};

struct rbtree_map {
	TOID(struct tree_map_node) sentinel;
	TOID(struct tree_map_node) root;
};
int rbtree_map_check(PMEMobjpool *pop, TOID(struct rbtree_map) map);
int rbtree_map_create(PMEMobjpool *pop, TOID(struct rbtree_map) *map,
	void *arg);
int rbtree_map_destroy(PMEMobjpool *pop, TOID(struct rbtree_map) *map);
int rbtree_map_insert(PMEMobjpool *pop, TOID(struct rbtree_map) map,
	uint64_t key, PMEMoid value);
int rbtree_map_insert_new(PMEMobjpool *pop, TOID(struct rbtree_map) map,
		uint64_t key, size_t size, unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg);
PMEMoid rbtree_map_remove(PMEMobjpool *pop, TOID(struct rbtree_map) map,
		uint64_t key);
int rbtree_map_remove_free(PMEMobjpool *pop, TOID(struct rbtree_map) map,
		uint64_t key);
int rbtree_map_clear(PMEMobjpool *pop, TOID(struct rbtree_map) map);
PMEMoid rbtree_map_get(PMEMobjpool *pop, TOID(struct rbtree_map) map,
		uint64_t key);
int rbtree_map_lookup(PMEMobjpool *pop, TOID(struct rbtree_map) map,
		uint64_t key);
int rbtree_map_foreach(PMEMobjpool *pop, TOID(struct rbtree_map) map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg);
int rbtree_map_is_empty(PMEMobjpool *pop, TOID(struct rbtree_map) map);

#endif /* RBTREE_MAP_H */
