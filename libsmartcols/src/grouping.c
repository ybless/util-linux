/*
 * Copyright (C) 2018 Karel Zak <kzak@redhat.com>
 */
#include "smartcolsP.h"

/**
 * SECTION: grouping
 * @title: Grouping
 * @short_description: lines grouing
 *
 * Lines groups manipulation API.
 */

/* Private API */
void scols_ref_group(struct libscols_group *gr)
{
	if (gr)
		gr->refcount++;
}

void scols_group_remove_children(struct libscols_group *gr)
{
	if (!gr)
		return;

	while (!list_empty(&gr->gr_children)) {
		struct libscols_line *ln = list_entry(gr->gr_children.next,
						struct libscols_line, ln_children);

		DBG(GROUP, ul_debugobj(gr, "remove child"));
		list_del_init(&ln->ln_children);
		scols_unref_line(ln);
	}
}

void scols_group_remove_members(struct libscols_group *gr)
{
	if (!gr)
		return;

	while (!list_empty(&gr->gr_members)) {
		struct libscols_line *ln = list_entry(gr->gr_members.next,
						struct libscols_line, ln_groups);

		DBG(GROUP, ul_debugobj(gr, "remove member"));
		list_del_init(&ln->ln_groups);

		scols_unref_group(ln->group);
		ln->group = NULL;

		scols_unref_line(ln);
	}
}

/* note group has to be already without members to deallocate */
void scols_unref_group(struct libscols_group *gr)
{
	if (gr && --gr->refcount <= 0) {
		DBG(GROUP, ul_debugobj(gr, "dealloc"));
		scols_group_remove_children(gr);
		list_del(&gr->gr_groups);
		free(gr);
		return;
	}
}

static void groups_fix_members_order(struct libscols_line *ln)
{
	struct libscols_iter itr;
	struct libscols_line *child;

	if (ln->group)
		list_add_tail(&ln->ln_groups, &ln->group->gr_members);

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_line_next_child(ln, &itr, &child) == 0)
		groups_fix_members_order(child);
}

void scols_groups_fix_members_order(struct libscols_table *tb)
{
	struct libscols_iter itr;
	struct libscols_line *ln;
	struct libscols_group *gr;

	/* remove all from groups lists */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_group(tb, &itr, &gr) == 0) {
		while (!list_empty(&gr->gr_members)) {
			struct libscols_line *ln = list_entry(gr->gr_members.next,
						struct libscols_line, ln_groups);
			list_del_init(&ln->ln_groups);
		}
	}

	/* add again to the groups list in order we walk in tree */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_line(tb, &itr, &ln) == 0) {
		if (ln->parent || ln->parent_group)
			continue;
		groups_fix_members_order(ln);
	}
}

/**
 * scols_table_group_lines:
 * @tb: a pointer to a struct libscols_table instance
 * @a: (wanted) group member
 * @b: (wanted) group member
 *
 * This function add @a, @b or both to the group. The group is represented by
 * @a or @b group member. If the group is not yet defined than a new one is
 * allocated and @a and @b added the new group.
 *
 * The @a or @b maybe a NULL -- in this case only a new group is allocated and
 * non-NULL line is added to the group.
 *
 * Note that the same line cannot be member of more groups.
 *
 * Returns: 0, a negative value in case of an error.
 *
 * Since: 2.34
 */
int scols_table_group_lines(	struct libscols_table *tb,
				struct libscols_line *a,
				struct libscols_line *b)
{
	struct libscols_group *gr = NULL;

	if (!tb || (!a && !b))
		return -EINVAL;
	if (a && b && a->group && b->group && a->group != b->group)
		return -EINVAL;

	gr = a ? a->group :
	     b ? b->group : NULL;

	/* create a new group */
	if (!gr) {
		gr = calloc(1, sizeof(*gr));
		if (!gr)
			return -ENOMEM;
		DBG(GROUP, ul_debugobj(gr, "alloc"));
		gr->refcount = 1;
		INIT_LIST_HEAD(&gr->gr_members);
		INIT_LIST_HEAD(&gr->gr_children);
		INIT_LIST_HEAD(&gr->gr_groups);
		INIT_LIST_HEAD(&gr->gr_groups_active);

		/* add group to the table */
		list_add_tail(&gr->gr_groups, &tb->tb_groups);
	}

	/* add group member */
	if (a && !a->group) {
		DBG(GROUP, ul_debugobj(gr, "add member"));
		a->group = gr;
		scols_ref_group(gr);

		list_add_tail(&a->ln_groups, &gr->gr_members);
		scols_ref_line(a);
	}

	/* add group member */
	if (b && !b->group) {
		DBG(GROUP, ul_debugobj(gr, "add member"));
		b->group = gr;
		scols_ref_group(gr);

		list_add_tail(&b->ln_groups, &gr->gr_members);
		scols_ref_line(b);
	}

	return 0;
}

/**
 * scols_line_link_groups:
 * @ln: line instance
 * @member: group member
 *
 * Define @ln as child of group represented by group @member.
 *
 * Returns: 0, a negative value in case of an error.
 *
 * Since: 2.34
 */
int scols_line_link_group(struct libscols_line *ln, struct libscols_line *member)
{
	if (!ln || !member || !member->group || ln->parent)
		return -EINVAL;

	DBG(GROUP, ul_debugobj(member->group, "add child"));

	list_add_tail(&ln->ln_children, &member->group->gr_children);
	scols_ref_line(ln);
	ln->parent_group = member->group;
	return 0;
}
