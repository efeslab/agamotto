#ifndef	RBTREE_MAP_H
#define	RBTREE_MAP_H

#include "tree_map.h"

#include <libpmemobj.h>

TOID_DECLARE(struct tree_map_node, TREE_MAP_TYPE_OFFSET + 1);

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

struct tree_map {
	TOID(struct tree_map_node) sentinel;
	TOID(struct tree_map_node) root;
};

#endif /* RBTREE_MAP_H */
