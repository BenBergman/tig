/* Copyright (c) 2006-2013 Jonas Fonseca <fonseca@diku.dk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "tig.h"
#include "graph.h"

DEFINE_ALLOCATOR(realloc_graph_columns, struct graph_column, 32)
DEFINE_ALLOCATOR(realloc_graph_symbols, struct graph_symbol, 1)

//static size_t get_free_graph_color(struct graph *graph)
//{
//	size_t i, free_color;
//
//	for (free_color = i = 0; i < ARRAY_SIZE(graph->colors); i++) {
//		if (graph->colors[i] < graph->colors[free_color])
//			free_color = i;
//	}
//
//	graph->colors[free_color]++;
//	return free_color;
//}

void
done_graph(struct graph *graph)
{
	free(graph->row.columns);
	free(graph->parents.columns);
	memset(graph, 0, sizeof(*graph));
}

#define graph_column_has_commit(col) ((col)->id[0])

static size_t
graph_find_column_by_id(struct graph_row *row, const char *id)
{
	size_t free_column = row->size;
	size_t i;

	for (i = 0; i < row->size; i++) {
		if (!graph_column_has_commit(&row->columns[i]))
			free_column = i;
		else if (!strcmp(row->columns[i].id, id))
			return i;
	}

	return free_column;
}

static struct graph_column *
graph_insert_column(struct graph *graph, struct graph_row *row, size_t pos, const char *id)
{
	struct graph_column *column;

	if (!realloc_graph_columns(&row->columns, row->size, 1))
		return NULL;

	column = &row->columns[pos];
	if (pos < row->size) {
		memmove(column + 1, column, sizeof(*column) * (row->size - pos));
	}

	row->size++;
	memset(column, 0, sizeof(*column));
	string_copy_rev(column->id, id);
	column->symbol.boundary = !!graph->is_boundary;

	return column;
}

struct graph_column *
graph_add_parent(struct graph *graph, const char *parent)
{
	return graph_insert_column(graph, &graph->parents, graph->parents.size, parent);
}

static void
ben_debug_printf(struct graph *graph, const char *format, ...)
{
	return;
//	if (strcmp(graph->id, "19c3ac60ede476130e693ece1752a29fa4e13512") == 0
////			|| strcmp(graph->id, "158a522c5740123fd1144f9045ef0a419dfcf090") == 0
////			|| strcmp(graph->id, "aac64c17cd7dd8c6ceba5738cc27a7eee48b8e59") == 0
//			|| strcmp(graph->id, "A") == 0
//			|| strcmp(graph->id, "604da3b78777bf69d01d214f203f3be8ee258f67") == 0)
	{
		va_list args;
		va_start (args, format);
		vprintf (format, args);
		va_end (args);
	}
}

static void
ben_debug_print_row(struct graph *graph, struct graph_row *row)
{
	int i;
	for (i = 0; i < row->size; i++) {
		ben_debug_printf(graph, "%s ", row->columns[i].id);
	}
}

static bool
graph_needs_expansion(struct graph *graph)
{
//	ben_debug_printf(graph, "g->pos: %d  g->p.s: %d  g->r.s: %d\n", graph->position, graph->parents.size, graph->row.size);
	return graph->position + graph->parents.size > graph->row.size;
#if 0
	return graph->parents.size > 1
	    && graph->expanded < graph->parents.size;
#endif
}

static bool
graph_expand(struct graph *graph)
{
//	ben_debug_printf(graph, "Expanding for commit %s...\n", graph->id);
	while (graph_needs_expansion(graph)) {
		if (!graph_insert_column(graph, &graph->prev_row, graph->prev_row.size, ""))
			return FALSE;
		if (!graph_insert_column(graph, &graph->row, graph->row.size, ""))
			return FALSE;
		if (!graph_insert_column(graph, &graph->next_row, graph->next_row.size, ""))
			return FALSE;
		graph->expanded++;
	}

	return TRUE;
}

static bool
graph_needs_collapsing(struct graph *graph)
{
	return graph->row.size > 1
	    && !graph_column_has_commit(&graph->row.columns[graph->row.size - 1]);
}

static bool
graph_collapse(struct graph *graph)
{
	while (graph_needs_collapsing(graph)) {
		graph->prev_row.size--;
		graph->row.size--;
		graph->next_row.size--;
	}

	return TRUE;
}

static void
graph_reorder_parents(struct graph *graph)
{
	struct graph_row *row = &graph->row;
	struct graph_row *parents = &graph->parents;
	int i;

	if (parents->size == 1)
		return;

	for (i = 0; i < parents->size; i++) {
		struct graph_column *column = &parents->columns[i];
		size_t match = graph_find_column_by_id(row, column->id);

		if (match < graph->position && graph_column_has_commit(&row->columns[match])) {
			//die("Reorder: %s -> %s", graph->commit->id, column->id);
//			row->columns[match].symbol.initial = 1;
		}
	}
}

static void
graph_canvas_append_symbol(struct graph *graph, struct graph_symbol *symbol)
{
	struct graph_canvas *canvas = graph->canvas;

	if (realloc_graph_symbols(&canvas->symbols, canvas->size, 1))
		canvas->symbols[canvas->size++] = *symbol;
}

static void
graph_generate_next_row(struct graph *graph)
{
	struct graph_row *row = &graph->next_row;
	struct graph_row *parents = &graph->parents;

	struct graph_column *current = &row->columns[graph->position];
	if (!strcmp(current->id, graph->id)) {
		current->id[0] = 0;
	}

	ben_debug_print_row(graph, row);
	ben_debug_printf(graph, "<- after first action (clear current id up to and including current pos)\n");

	int i;
	for (i = 0; i < row->size; i++) {
		size_t match = graph_find_column_by_id(row, row->columns[i].id);
		if (match < i)
			row->columns[i].id[0] = 0;
	}
	ben_debug_print_row(graph, row);
	ben_debug_printf(graph, "<- after first loop (clear duplicate ids)\n");

	for (i = 0; i < parents->size; i++) {
		struct graph_column *new = &parents->columns[i];
		if (graph_column_has_commit(new)) {
			if (row->columns[graph->position].id[0] == 0) {
				row->columns[graph->position] = *new;
			} else {
				size_t match = graph_find_column_by_id(row, new->id);
				if (match == row->size) {
					graph_insert_column(graph, row, row->size, new->id);
					graph_insert_column(graph, &graph->row, graph->row.size, "");
				} else {
					row->columns[match] = *new;
				}
			}
		}
	}
	ben_debug_print_row(graph, row);
	ben_debug_printf(graph, "<- after second loop (insert parents)\n");

	for (i = graph->position; i < row->size; i++) {
		struct graph_column *old = &row->columns[i];
		if (!strcmp(old->id, graph->id)) {
			old->id[0] = 0;
		}
	}
	ben_debug_print_row(graph, row);
	ben_debug_printf(graph, "<- after third loop (clear current id from current position to end of row)\n");

	int last = row->size - 1;
	while (strcmp(row->columns[last].id, graph->id) != 0 && strcmp(row->columns[last].id, row->columns[last - 1].id) == 0) {
		row->columns[last].id[0] = 0;
		last--;
	}
	ben_debug_print_row(graph, row);
	ben_debug_printf(graph, "<- after removing trailing duplicate id\n");

	if (!graph_column_has_commit(&row->columns[graph->position])) {
		size_t min_pos = row->size;
		struct graph_column *min_commit = &parents->columns[0];
		int i;
		for (i = 0; i < graph->parents.size; i++) {
			if (graph_column_has_commit(&parents->columns[i])) {
				size_t match = graph_find_column_by_id(row, parents->columns[i].id);
				if (match < min_pos) {
					min_pos = match;
					min_commit = &parents->columns[i];
				}
			}
		}
		row->columns[graph->position] = *min_commit;
	}
	ben_debug_print_row(graph, row);
	ben_debug_printf(graph, "<- after emergency copy...?\n");

	for (i = row->size - 1; i >= 0; i--) {
		if (!graph_column_has_commit(&row->columns[i])) {
			row->columns[i] = *(&row->columns[i+1]);
		}
	}
}

static void
graph_commit_next_row(struct graph *graph)
{
	int i;
	for (i = 0; i < graph->row.size; i++) {
		graph->prev_row.columns[i] = graph->row.columns[i];
		if (i == graph->position)
			graph->prev_row.columns[i] = graph->next_row.columns[i];
		graph->row.columns[i] = graph->next_row.columns[i];
	}
}

//static bool
//is_parent(struct graph *graph, const char *id)
//{
//	int i;
//
//	for (i = 0; i < graph->parents.size; i++) {
//		if (!strcmp(id, graph->parents.columns[i].id))
//			return true;
//	}
//	return false;
//}

static bool
continued_down(struct graph_row *row, struct graph_row *next_row, int pos)
{
	if (strcmp(row->columns[pos].id, next_row->columns[pos].id) == 0)
		return true;

	return false;
}

static bool
continued_right(struct graph_row *row, int pos, int commit_pos)
{
	int i, end;
	if (pos < commit_pos)
		end = commit_pos;
	else
		end = row->size;

	for (i = pos + 1; i < end; i++)
		if (strcmp(row->columns[pos].id, row->columns[i].id) == 0)
			return true;

	return false;
}

static bool
continued_left(struct graph_row *row, int pos, int commit_pos)
{
	int i, start;
	if (pos < commit_pos)
		start = 0;
	else
		start = commit_pos + 1;

	for (i = start; i < pos; i++)
		if (strcmp(row->columns[pos].id, row->columns[i].id) == 0)
			return true;

	return false;
}

static bool
parent_down(struct graph_row *parents, struct graph_row *next_row, int pos)
{
	int parent;
	for (parent = 0; parent < parents->size; parent++)
		if (graph_column_has_commit(&parents->columns[parent]) && strcmp(parents->columns[parent].id, next_row->columns[pos].id) == 0)
			return true;

	return false;
}

static bool
parent_right(struct graph_row *parents, struct graph_row *next_row, int pos)
{
	int parent, i;
	for (parent = 0; parent < parents->size; parent++)
		for (i = pos + 1; i < next_row->size; i++)
			if (graph_column_has_commit(&parents->columns[parent]) && strcmp(parents->columns[parent].id, next_row->columns[i].id) == 0)
				if (!continued_left(next_row, i, next_row->size))
					return true;

	return false;
}

static bool
flanked(struct graph_row *row, int pos, int commit_pos)
{
	int before, after;
	int min = 0;
	int max = row->size;
	if (pos < commit_pos)
		max = commit_pos;
	else
		min = commit_pos;

	for (before = min; before < pos; before++)
		for (after = pos + 1; after < max; after++)
			if (strcmp(row->columns[before].id, row->columns[after].id) == 0)
				return true;

	return false;
}

static int
commits_in_row(struct graph_row *row)
{
	int count = 0;
	int i;
	for (i = 0; i < row->size;i++)
		if (graph_column_has_commit(&row->columns[i]))
			count++;

	return count;
}

static bool
graph_insert_parents(struct graph *graph)
{
	struct graph_row *prev_row = &graph->prev_row;
	struct graph_row *row = &graph->row;
	struct graph_row *next_row = &graph->next_row;
	struct graph_row *parents = &graph->parents;
	int pos;

	assert(!graph_needs_expansion(graph));

	graph_generate_next_row(graph);

//	ben_debug_print_row(graph, prev_row);
//	ben_debug_printf(graph, "<- prev_row\n");
	ben_debug_print_row(graph, row);
	ben_debug_printf(graph, "<- row\n");
//	ben_debug_print_row(graph, next_row);
//	ben_debug_printf(graph, "<- next_row\n");

//	ben_debug_print_row(graph, row);
//	ben_debug_printf(graph, "<- post next generation\n");

	for (pos = 0; pos < row->size; pos++) {
		struct graph_column *column = &row->columns[pos];
		struct graph_symbol symbol = column->symbol;

		if (pos == graph->position) {
			symbol.commit = true;

			if (commits_in_row(parents) < 1)
				symbol.initial = true;

			if (commits_in_row(parents) > 1)
				symbol.merge = true;
		}

		symbol.continued_down = continued_down(row, next_row, pos);
		symbol.continued_up = continued_down(prev_row, row, pos);
		symbol.continued_right = continued_right(row, pos, graph->position);
		symbol.continued_left = continued_left(row, pos, graph->position);
		symbol.parent_down = parent_down(parents, next_row, pos);
		symbol.parent_right = (pos > graph->position && parent_right(parents, next_row, pos));
		symbol.flanked = flanked(row, pos, graph->position);
		symbol.next_right = continued_right(next_row, pos, 0);

		graph_canvas_append_symbol(graph, &symbol);
	}

//	ben_debug_printf(graph, "Printing parents...\n");
//	ben_debug_print_row(graph, parents);
//	ben_debug_printf(graph, "\nparents->size: %d\n", parents->size);
//	ben_debug_printf(graph, "commits_in_row(parents): %d\n", commits_in_row(parents));

//	for (pos = 0; pos < graph->position; pos++) {
//		struct graph_column *column = &row->columns[pos];
//		struct graph_symbol symbol = column->symbol;
//
//		if (graph_column_has_commit(column)) {
//			size_t match = graph_find_column_by_id(parents, column->id);
//
//			if (match < parents->size) {
//				column->symbol.initial = 1;
//			}
//
//			symbol.branch = 1;
//		}
//		symbol.vbranch = !!branched;
//		if (!strcmp(column->id, graph->id)) {
//			branched = TRUE;
//			column->id[0] = 0;
//		}
//
//		graph_canvas_append_symbol(graph, &symbol);
//	}
//
//	for (; pos < graph->position + parents->size; pos++) {
//		struct graph_column *old = &row->columns[pos];
//		struct graph_column *new = &next_row->columns[pos];
//		struct graph_symbol symbol = old->symbol;
//
//		symbol.merge = !!merge;
//
//		if (pos == graph->position) {
//			symbol.commit = 1;
//			/*
//			if (new->symbol->boundary) {
//				symbol.boundary = 1;
//			} else*/
//			if (!graph_column_has_commit(new)) {
//				symbol.initial = 1;
//			}
//
//		} else if (graph_column_has_commit(old) && !strcmp(old->id, new->id) && orig_size == row->size) {
//			symbol.vbranch = 1;
//			symbol.branch = 1;
//			if (!is_parent(graph, old->id))
//				symbol.merge = 0;
//
//		} else if (parents->size > 1) {
//			symbol.merge = 1;
//			if (!graph_column_has_commit(new))
//				symbol.merge = 0;
//			if (!strcmp(old->id, graph->id)) {
//				symbol.branch = 1;
//				symbol.branched = 1;
//			}
//			if (pos < graph->position + parents->size - 1)
//				symbol.vbranch = 1;
//			if (!is_parent(graph, new->id)) {
//				symbol.merge = 0;
//				symbol.branch = 1;
//				if (!symbol.branched)
//					symbol.vbranch = 1;
//			}
//
//		} else if (graph_column_has_commit(old)) {
//			symbol.branch = 1;
//		}
//
//		graph_canvas_append_symbol(graph, &symbol);
//		if (!graph_column_has_commit(old))
//			new->symbol.color = get_free_graph_color(graph);
//	}
//
//	for (; pos < row->size; pos++) {
//		bool too = !strcmp(row->columns[row->size - 1].id, graph->id);
//		struct graph_symbol symbol = row->columns[pos].symbol;
//
//		symbol.vbranch = !!too;
//		if (row->columns[pos].id[0]) {
//			symbol.branch = 1;
//			if (!strcmp(row->columns[pos].id, graph->id)) {
//				symbol.branched = 1;
//				if (too && pos != row->size - 1) {
//					symbol.vbranch = 1;
//				} else {
//					symbol.vbranch = 0;
//				}
//				row->columns[pos].id[0] = 0;
//			}
//			int i;
//			for (i = pos + 1; i < row->size; i++) {
//				if (strcmp(next_row->columns[i].id, row->columns[i].id) != 0) {
//					if (strcmp(row->columns[pos].id, row->columns[i].id) == 0) {
//						symbol.collapse = 1;
//						break;
//					}
//					int parent;
//					for (parent = 0; parent < graph->parents.size; parent++) {
//						if (strcmp(graph->parents.columns[parent].id, next_row->columns[i].id) == 0) {
//							symbol.vbranch = 1;
//						}
//					}
//				}
//			}
//			if (strcmp(row->columns[pos].id, next_row->columns[pos].id) != 0) {
//				symbol.branched = 1;
//			}
//		} else if (parents->size > 1) {
//			symbol.merge = 1;
//		}
//		graph_canvas_append_symbol(graph, &symbol);
//	}

	graph_commit_next_row(graph);

	graph->parents.size = graph->expanded = graph->position = 0;

	return TRUE;
}

bool
graph_render_parents(struct graph *graph)
{
	if (!graph_expand(graph))
		return FALSE;
	graph_reorder_parents(graph);
	graph_insert_parents(graph);
	if (!graph_collapse(graph))
		return FALSE;

	return TRUE;
}

bool
graph_add_commit(struct graph *graph, struct graph_canvas *canvas,
		 const char *id, const char *parents, bool is_boundary)
{
	graph->position = graph_find_column_by_id(&graph->row, id);
	graph->id = id;
	graph->canvas = canvas;
	graph->is_boundary = is_boundary;

	while ((parents = strchr(parents, ' '))) {
		parents++;
		if (!graph_add_parent(graph, parents))
			return FALSE;
		graph->has_parents = TRUE;
	}

	if (graph->parents.size == 0 &&
	    !graph_add_parent(graph, ""))
		return FALSE;

	return TRUE;
}

const char *
graph_symbol_to_utf8(struct graph_symbol *symbol)
{
	if (symbol->commit) {
		if (symbol->boundary)
			return " ◯";
		else if (symbol->initial)
			return " ◎";
		else if (symbol->merge)
			return " ●";
		return " ●";
	}

	if (symbol->continued_down) {
		if (symbol->continued_right) {
			if (symbol->continued_up) {
				return " ├";
			}
			return " ┌";
		}
		if (symbol->parent_right || symbol->flanked) {
			return "─│";
		}
		if (symbol->continued_left) {
			return "─┘";
		}
		return " │";
	}

	if (symbol->parent_down) {
		if (symbol->parent_right) {
			return "─┬";
		}
		return "─┐";
	}

	if (symbol->parent_right || (symbol->continued_right && symbol->continued_right)) {
		return "──";
	}

	if (!symbol->continued_right && !symbol->continued_down) {// && symbol->continued_left) {
		return "─┘";
	}

	return "  ";



	if (symbol->merge) {
		if (symbol->branch) {
			return "─┤";
		}
		if (symbol->vbranch)
			return "─┬";
		return "─┐";
	}

	if (symbol->branch) {
		if (symbol->branched) {
			if (symbol->vbranch)
				return "─┴";
			return "─┘";
		}
		if (symbol->vbranch)
			return "─│";
		if (symbol->collapse)
			return " ┌";
		return " │";
	}

	if (symbol->vbranch)
		return "──";

	return "  ";
}

const chtype *
graph_symbol_to_chtype(struct graph_symbol *symbol)
{
	static chtype graphics[2];

	if (symbol->commit) {
		graphics[0] = ' ';
		if (symbol->boundary)
			graphics[1] = 'o';
		else if (symbol->initial)
			graphics[1] = 'I';
		else if (symbol->merge)
			graphics[1] = 'M';
		else
			graphics[1] = 'o'; //ACS_DIAMOND; //'*';
		return graphics;
	}

	if (symbol->merge) {
		graphics[0] = ACS_HLINE;
		if (symbol->branch)
			graphics[1] = ACS_RTEE;
		else
			graphics[1] = ACS_URCORNER;
		return graphics;
	}

	if (symbol->branch) {
		graphics[0] = ACS_HLINE;
		if (symbol->branched) {
			if (symbol->vbranch)
				graphics[1] = ACS_BTEE;
			else
				graphics[1] = ACS_LRCORNER;
			return graphics;
		}

		if (!symbol->vbranch)
			graphics[0] = ' ';
		graphics[1] = ACS_VLINE;
		if (symbol->collapse) {
			graphics[0] = ' ';
			graphics[1] = ACS_ULCORNER;
		}
		return graphics;
	}

	if (symbol->vbranch) {
		graphics[0] = graphics[1] = ACS_HLINE;
	} else
		graphics[0] = graphics[1] = ' ';

	return graphics;
}

const char *
graph_symbol_to_ascii(struct graph_symbol *symbol)
{
	if (symbol->commit) {
		if (symbol->boundary)
			return " o";
		else if (symbol->initial)
			return " I";
		else if (symbol->merge)
			return " M";
		return " *";
	}

	if (symbol->merge) {
		if (symbol->branch)
			return "-+";
		return "-.";
	}

	if (symbol->branch) {
		if (symbol->branched) {
			if (symbol->vbranch)
				return "-+";
			return "-'";
		}
		if (symbol->vbranch)
			return "-|";
		if (symbol->collapse)
			return " .";
		return " |";
	}

	if (symbol->vbranch)
		return "--";

	return "  ";
}

/* vim: set ts=8 sw=8 noexpandtab: */
