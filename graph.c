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
		if (!graph_column_has_commit(&row->columns[i]) && free_column == row->size)
			free_column = i;
		else if (!strcmp(row->columns[i].id, id))
			return i;
	}

	return free_column;
}

static size_t
graph_find_free_column(struct graph_row *row)
{
	size_t i;

	for (i = 0; i < row->size; i++) {
		if (!graph_column_has_commit(&row->columns[i]))
			return i;
	}

	return row->size;
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

static bool
graph_needs_expansion(struct graph *graph)
{
	return graph->position + graph->parents.size > graph->row.size;
#if 0
	return graph->parents.size > 1
	    && graph->expanded < graph->parents.size;
#endif
}

static bool
graph_expand(struct graph *graph)
{
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

	int i;
	for (i = 0; i < row->size; i++) {
		if (strcmp(row->columns[i].id, graph->id) == 0) {
			row->columns[i].id[0] = 0;
		}
	}

	for (i = 0; i < parents->size; i++) {
		struct graph_column *new = &parents->columns[i];
		if (graph_column_has_commit(new)) {
			size_t match = graph_find_free_column(row);
			if (match == row->size && row->columns[row->size - 1].id) {
				graph_insert_column(graph, row, row->size, new->id);
				graph_insert_column(graph, &graph->row, graph->row.size, "");
				graph_insert_column(graph, &graph->prev_row, graph->prev_row.size, "");
			} else {
				row->columns[match] = *new;
			}
		}
	}

	for (i = graph->position; i < row->size; i++) {
		struct graph_column *old = &row->columns[i];
		if (!strcmp(old->id, graph->id)) {
			old->id[0] = 0;
		}
	}

	int last = row->size - 1;
	while (
			last > graph->position + 1
			&& strcmp(row->columns[last].id, graph->id) != 0
			&& strcmp(row->columns[last].id, row->columns[last - 1].id) == 0
			&& strcmp(row->columns[last - 1].id, graph->prev_row.columns[last - 1].id) != 0
			) {
		row->columns[last].id[0] = 0;
		last--;
	}

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
		graph->prev_position = graph->position;
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
shift_left(struct graph_row *row, struct graph_row *prev_row, int pos)
{
	int i;
	if (!graph_column_has_commit(&row->columns[pos]))
		return false;

	for (i = pos - 1; i >= 0; i--) {
		if (graph_column_has_commit(&row->columns[i]))
			if (strcmp(row->columns[i].id, row->columns[pos].id) == 0)
				if (!continued_down(prev_row, row, i))
					return true;
	}

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
		start = commit_pos;

	for (i = start; i < pos; i++)
		if (graph_column_has_commit(&row->columns[i]) && strcmp(row->columns[pos].id, row->columns[i].id) == 0)
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
parent_right(struct graph_row *parents, struct graph_row *row, struct graph_row *next_row, int pos)
{
	int parent, i;
	for (parent = 0; parent < parents->size; parent++)
		if (graph_column_has_commit(&parents->columns[parent]))
			for (i = pos + 1; i < next_row->size; i++)
				if (strcmp(parents->columns[parent].id, next_row->columns[i].id) == 0)
					if (strcmp(parents->columns[parent].id, row->columns[i].id) != 0)
			//		if (!continued_left(next_row, i, next_row->size))
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

	for (pos = 0; pos < row->size; pos++) {
		struct graph_column *column = &row->columns[pos];
		struct graph_symbol symbol = column->symbol;

		if (pos == graph->position) {
			if (next_row->columns[pos].symbol.boundary)
				symbol.boundary = true;

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
		symbol.continued_up_left = continued_left(prev_row, pos, prev_row->size);
		symbol.below_commit = pos == graph->prev_position;
		symbol.parent_down = parent_down(parents, next_row, pos);
		symbol.parent_right = (pos > graph->position && parent_right(parents, row, next_row, pos));
		symbol.flanked = flanked(row, pos, graph->position);
		symbol.next_right = continued_right(next_row, pos, 0);
		symbol.matches_commit = (strcmp(column->id, graph->id) == 0);
		symbol.shift_left = shift_left(row, prev_row, pos);
		symbol.new_column = (!graph_column_has_commit(&prev_row->columns[pos]));
		symbol.empty = (!graph_column_has_commit(&row->columns[pos]));

		graph_canvas_append_symbol(graph, &symbol);
	}

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

const bool
graph_symbol_forks(struct graph_symbol *symbol)
{
	if (!symbol->continued_down)
		return false;

	if (!symbol->continued_right)
		return false;

	if (!symbol->continued_up)
		return false;

	return true;
}

const bool
graph_symbol_cross_over(struct graph_symbol *symbol)
{
	if (symbol->empty)
		return false;

	if (!symbol->continued_down)
		return false;

	if (symbol->parent_right)
		return true;

	if (symbol->flanked)
		return true;

	return false;
}

const bool
graph_symbol_turn_left(struct graph_symbol *symbol)
{
	if (symbol->continued_down)
		return false;

	if (symbol->continued_right)
		return false;

	if (symbol->continued_up || symbol->new_column || symbol->below_commit) {
		if (symbol->matches_commit)
			return true;

		if (symbol->shift_left)
			return true;
	}

//	if (symbol->continued_down)
//		if (symbol->continued_left)
//			return true;
//
//	if (symbol->continued_up)
//		if (symbol->continued_left)
//			if (!symbol->continued_up_left)
//				return true;
//
//	if (!symbol->parent_down)
//		if (!symbol->continued_right)
//			if (!symbol->continued_down)
//				return true;

	return false;
}

const bool
graph_symbol_turn_down(struct graph_symbol *symbol)
{
	if (!symbol->continued_down)
		return false;

	if (!symbol->continued_right)
		return false;

	return true;
}

const bool
graph_symbol_merge(struct graph_symbol *symbol)
{
	if (symbol->continued_down)
		return false;

	if (!symbol->parent_down)
		return false;

	if (symbol->parent_right)
		return false;

	return true;
}

const bool
graph_symbol_multi_merge(struct graph_symbol *symbol)
{
	if (!symbol->parent_down)
		return false;

	if (!symbol->parent_right)
		return false;

	return true;
}

const bool
graph_symbol_vertical_bar(struct graph_symbol *symbol)
{
	if (symbol->empty)
		return false;

	if (symbol->continued_up)
		if (symbol->continued_down)
			return true;

	if (!symbol->continued_down)
		return false;

	if (symbol->parent_right)
		return false;

	if (symbol->flanked)
		return false;

	if (symbol->continued_left)
		return false;

	if (symbol->continued_right)
		return false;

	return true;
}

const bool
graph_symbol_horizontal_bar(struct graph_symbol *symbol)
{
	if (symbol->continued_down)
		return false;

	if (!symbol->parent_right && !symbol->continued_right)
		return false;

	if ((symbol->continued_up && !symbol->continued_up_left))
		return false;

	if (!symbol->below_commit)
		return true;

	return false;
}

const bool
graph_symbol_multi_branch(struct graph_symbol *symbol)
{
	if (symbol->continued_down)
		return false;

	if (!symbol->continued_right)
		return false;

	if (symbol->continued_up || symbol->new_column || symbol->below_commit) {
		if (symbol->matches_commit)
			return true;

		if (symbol->shift_left)
			return true;
	}

//	if (!symbol->continued_down)
//		if (symbol->parent_right || symbol->continued_right) {
//			if ((symbol->continued_up && !symbol->continued_up_left))
//				return true;
//
//			if (symbol->below_commit)
//				return true;
//		}

	return false;
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

	if (graph_symbol_cross_over(symbol))
		return "─│";

	if (graph_symbol_vertical_bar(symbol))
		return " │";

	if (graph_symbol_turn_left(symbol))
		return "─┘";

	if (graph_symbol_multi_branch(symbol))
		return "─┴";

	if (graph_symbol_horizontal_bar(symbol))
		return "──";

	if (graph_symbol_forks(symbol))
		return " ├";

	if (graph_symbol_turn_down(symbol))
		return " ┌";

	if (graph_symbol_merge(symbol))
		return "─┐";

	if (graph_symbol_multi_merge(symbol))
		return "─┬";

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
