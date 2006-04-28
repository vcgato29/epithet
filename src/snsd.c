/* This file is part of Netsukuku
 * (c) Copyright 2006 Andrea Lo Pumo aka AlpT <alpt@freaknet.org>
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This source code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Please refer to the GNU Public License for more details.
 *
 * You should have received a copy of the GNU Public License along with
 * this source code; if not, write to:
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * --  
 * snsd.c
 * Scattered Name Service Digregation
 *
 * Here there are the main functions used to add/modify/delete new records in
 * the SNSD linked lists.
 * The functions which handle SNSD requests/replies are here too.
 * This code is also used by andna.c
 */

#include "includes.h"

#include "snsd.h"
#include "misc.h"
#include "xmalloc.h"
#include "log.h"

int net_family;

void snsd_init(int family)
{
	net_family=family;
}

/*
 * proto_to_8bit
 *
 * It returns the protocol number associated to `proto_name'.
 * (See the `proto_str' static array in snsd.h)
 *
 * If no protocol matched, 0 is returned.
 */
u_char proto_to_8bit(char *proto_name)
{
	int i;
	
	for(i=0; i<=256; i++) {
		if(!proto_str[i])
			break;
		if(!strcmp(proto_name, proto_str[i]))
			return i+1;
	}

	return 0;
}

/*
 *  *  *  SNSD structs functions  *  *  *
 */

snsd_service *snsd_find_service(snsd_service *sns, u_short service, 
				u_char proto)
{
	list_for(sns)
		if(sns->service == service && sns->proto == proto)
			return sns;
	return 0;
}

snsd_service *snsd_add_service(snsd_service **head, u_short service, 
				u_char proto)
{
	snsd_service *sns, *new;

	if((sns=snsd_find_service(*head, service, proto)))
		return sns;

	new=xmalloc(sizeof(snsd_service));
	setzero(new, snsd_service);
	new->service=service;
	new->proto=proto;
	
	*head=list_add(*head, new);

	return new;
}

snsd_prio *snsd_find_prio(snsd_prio *snp, u_char prio)
{
	list_for(snp)
		if(snp->prio == prio)
			return snp;
	return 0;
}

snsd_prio *snsd_add_prio(snsd_prio **head, u_char prio)
{
	snsd_prio *snp, *new;

	if((snp=snsd_find_prio(*head, prio)))
		return snp;

	new=xmalloc(sizeof(snsd_prio));
	setzero(new, snsd_prio);
	new->prio=prio;
	
	*head=list_add(*head, new);

	return new;
}

snsd_node *snsd_find_node_by_record(snsd_node *snd, u_int record[MAX_IP_INT])
{
	list_for(snd)
		if(!memcpy(snd->record, record, MAX_IP_SZ))
			return snd;
	return 0;
}


/*
 * snsd_add_node
 *
 * If `record' is not NULL, it searches for a snsd_node struct which has
 * the same `record' of the argument. If it is found, it is returned.
 * If it isn't found or `record' is NULL, it adds a new snsd_node struct 
 * in the `*head' llist and returns it.
 * `max_records' is the max number of records allowed in the llist. If no
 * empty place are left to add the new struct, 0 is returned.
 */
snsd_node *snsd_add_node(snsd_node **head, u_short *counter, 
			 u_short max_records, u_int record[MAX_IP_INT])
{
	snsd_node *snd;

	if(record && (snd=snsd_find_node_by_record(*head, record)))
		return snd;

	if(*counter >= max_records)
		/* The llist is full */
		return 0;

	snd=xmalloc(sizeof(snsd_node));
	setzero(snd, snsd_node);

	clist_add(head, counter, snd);

	return snd;
}

/*
 * snsd_add_first_node
 *
 * It adds a new node in the llist if `*head' or `*counter' is zero. 
 * The new node is returned.
 * If it isn't zero, it returns the first struct of the llist.
 */
snsd_node *snsd_add_first_node(snsd_node **head, u_short *counter,
				u_short max_records, u_int record[MAX_IP_INT])
{
	if(!(*head) || !(*counter))
		return snsd_add_node(head, counter, max_records, record);
	
	return *head;
}
				
void snsd_node_llist_del(snsd_node **head, u_short *counter)
{
	clist_destroy(head, counter);
}

void snsd_prio_llist_del(snsd_prio **head)
{
	snsd_prio *snp=(*head);
	u_short counter;

	list_for(snp)
		snsd_node_llist_del(&snp->node, &counter);
	clist_destroy(head, &counter);
	(*head)=(snsd_prio *)clist_init(&counter);
}

void snsd_service_llist_del(snsd_service **head)
{
	snsd_service *sns=(*head);
	int counter;
	
	list_for(sns)
		snsd_prio_llist_del(&sns->prio);
	clist_destroy(head, &counter);
}

		
/*
 *
 *  *  *  *  Pack/Unpack functions  *  *  *
 *
 */

/*
 * snsd_pack_node
 *
 * It packs the `node' snsd_node struct. The package is written in `pack'.
 * `free_sz' is the number of free bytes of `pack'. If `free_sz' is less than
 * SNSD_NODE_PACK_SZ, -1 is returned.
 * The number of bytes written in `pack' is returned.
 */
int snsd_pack_node(char *pack, size_t free_sz, snsd_node *node)
{
	char *buf=pack;
	
	if(free_sz < SNSD_NODE_PACK_SZ)
		return -1;

	memcpy(buf, node->record, MAX_IP_SZ);
	if(node->flags & SNSD_NODE_IP)
		inet_htonl((u_int *)buf, net_family);
	buf+=MAX_IP_SZ;

	memcpy(buf, &node->flags, sizeof(char));
	buf+=sizeof(char);

	memcpy(buf, &node->weight, sizeof(char));
	buf+=sizeof(char);

	return SNSD_NODE_PACK_SZ;
}

/*
 * snsd_unpack_node
 *
 * It returns the unpacked snsd_node struct.
 * `pack' is the buffer which contains the packed struct.
 * 
 * We are assuming that the total size of the package is >= SNSD_NODE_PACK_SZ.
 */
snsd_node *snsd_unpack_node(char *pack)
{
	snsd_node *snd;
	char *buf;
	
	snd=xmalloc(sizeof(snsd_node));
	setzero(snd, snsd_node);

	buf=pack;
	memcpy(snd->record, buf, MAX_IP_SZ);
	buf+=MAX_IP_SZ;
	
	memcpy(&snd->flags, buf, sizeof(char));
	buf+=sizeof(char);
	
	snd->weight=SNSD_WEIGHT((*((char *)(buf))));
	buf+=sizeof(char);

	if(snd->flags & SNSD_NODE_IP)
		inet_ntohl(snd->record, net_family);
	
	return snd;
}

/*
 * snsd_pack_all_nodes
 *
 * It packs all the snsd_node structs present in the `head' linked list.
 * The pack is written in `pack' which has to have enough space to contain the
 * packed llist. The size of the llist can be calculate using:
 * SNSD_NODE_LLIST_PACK_SZ(head)
 *
 * `pack_sz' is the number of free bytes allocated in `pack'.
 * 
 * The number of bytes written in `pack' is returned.
 * 
 * On error -1 is returned.
 */
int snsd_pack_all_nodes(char *pack, size_t pack_sz, snsd_node *head)
{
	struct snsd_node_llist_hdr *hdr;
	snsd_node *snd=head;
	int sz=0, wsz=0, counter=0;

	hdr=(struct snsd_node_llist_hdr *)pack;
	pack+=sizeof(struct snsd_node_llist_hdr);
	wsz+=sizeof(struct snsd_node_llist_hdr);
	
	list_for(snd) {
		sz=snsd_pack_node(pack, pack_sz-wsz, snd);
		if(sz <= 0)
			return -1;
		
		wsz+=sz; pack+=sz; counter++;
	}
	
	hdr->count=htons(counter);
	return wsz;
}

/*
 * snsd_unpack_all_nodes
 *
 * It unpacks a packed linked list of snsd_nodes, which is pointed by `pack'.
 * The number of unpacked structs is written in `nodes_counter'.
 *
 * `*unpacked_sz' is incremented by the number of unpacked bytes.
 * 
 * The head of the unpacked llist is returned.
 * On error 0 is returned.
 */
snsd_node *snsd_unpack_all_nodes(char *pack, size_t pack_sz, 
					size_t *unpacked_sz, int *nodes_counter)
{
	snsd_node *snd_head=0, *snd;
	char *buf=pack;
	int i, sz=0, counter;
	
#define INC_SZ_AND_CHECK_OVERFLOW(inc) {				\
	sz+=(inc);							\
	if(sz >= pack_sz) return 0;					\
}
	
	INC_SZ_AND_CHECK_OVERFLOW(sizeof(struct snsd_node_llist_hdr));
	
	counter=ntohs((*(short *)buf));
	buf+=sizeof(short);

	if(counter > SNSD_MAX_RECORDS)
		return 0;
	
	*nodes_counter=0;
	for(i=0; i<counter; i++) {
		INC_SZ_AND_CHECK_OVERFLOW(SNSD_NODE_PACK_SZ);
		
		snd=snsd_unpack_node(buf);
		buf+=SNSD_NODE_PACK_SZ;

		clist_add(&snd_head, nodes_counter, snd);
	}

	(*unpacked_sz)+=sz;
	return snd_head;
}

/*
 * snsd_pack_prio
 *
 * It packs the `prio' snsd_prio struct in the `pack' buffer, which has 
 * `free_sz' bytes allocated.
 * In the packs it includes the `prio'->node llist too.
 *
 * On error -1 is returned, otherwise the size of the package is returned.
 */
int snsd_pack_prio(char *pack, size_t free_sz, snsd_prio *prio)
{
	char *buf=pack;
	int wsz=0, sz=0;

	if(free_sz < SNSD_PRIO_PACK_SZ)
		return -1;
	
	*buf=prio->prio;
	buf+=sizeof(char);
	wsz+=sizeof(char);
	
	sz=snsd_pack_all_nodes(buf, free_sz-wsz, prio->node);
	if(sz <= 0)
		return -1;
	wsz+=sz;
	
	return wsz;
}

/*
 * snsd_unpack_prio
 *
 * It unpacks a packed snsd_prio struct and returns it.
 * `pack' is the package, which is `pack_sz' big.
 *
 * in the prio->node llist.
 *
 * `*unpacked_sz' is incremented by the number of unpacked bytes.
 * 
 * On error 0 is returned
 */
snsd_prio *snsd_unpack_prio(char *pack, size_t pack_sz, size_t *unpacked_sz)
{
	snsd_prio *snp;
	int tmp_counter;

	snp=xmalloc(sizeof(snsd_prio));
	setzero(snp, snsd_prio);
	
	snp->prio=*pack;
	pack+=sizeof(char);
	(*unpacked_sz)+=sizeof(char);

	snp->node=snsd_unpack_all_nodes(pack, pack_sz-sizeof(char), unpacked_sz,
			&tmp_counter);
	if(!snp->node)
		return 0;

	return snp;
}

/*
 * snsd_pack_all_prios
 *
 * It packs the whole snsd_prio linked list whose head is `head'.
 * `pack' is the buffer where the package will be stored.
 * `pack' is `pack_sz' bytes big.
 * Use SNSD_PRIO_PACK_SZ(head) to calculate the pack size.
 *
 * The number of bytes stored in `pack' is returned.
 *
 * On error -1 is returned.
 */
int snsd_pack_all_prios(char *pack, size_t pack_sz, snsd_prio *head)
{
	struct snsd_prio_llist_hdr *hdr;
	snsd_prio *snp=head;
	int sz=0, wsz=0, counter;

	hdr=(struct snsd_prio_llist_hdr *)pack;
	pack+=sizeof(struct snsd_prio_llist_hdr);
	wsz+=sizeof(struct snsd_prio_llist_hdr);
	
	list_for(snp) {
		sz=snsd_pack_prio(pack, pack_sz-wsz, snp);
		if(sz <= 0)
			return -1;
		wsz+=sz;
		pack+=sz;
		counter++;
	}
	
	hdr->count=htons(counter);
	return wsz;
}

/*
 * snsd_unpack_all_prios
 *
 * It unpacks the packed snsd_prio llist.
 * The head of the newly allocated llist is returned.
 * in the prio->node linked lists
 *
 * `*unpacked_sz' is incremented by the number of unpacked bytes.
 *
 * On error 0 is returned.
 */
snsd_prio *snsd_unpack_all_prios(char *pack, size_t pack_sz, size_t *unpacked_sz)
{
	snsd_prio *snp_head=0, *snp;
	char *buf=pack;
	int i, sz=0, tmp_sz, usz=0, counter, tmp_counter;
	
#define INC_SZ_AND_CHECK_OVERFLOW(inc) {				\
	sz+=(inc);							\
	if(sz >= pack_sz) return 0;					\
}
	INC_SZ_AND_CHECK_OVERFLOW(sizeof(struct snsd_prio_llist_hdr));
	
	counter=ntohs((*(short *)buf));
	buf+=sizeof(short);
	usz+=sizeof(short);

	if(counter > SNSD_MAX_RECORDS)
		return 0;
	
	for(i=0; i<counter; i++) {
		INC_SZ_AND_CHECK_OVERFLOW(SNSD_PRIO_PACK_SZ);
		
		tmp_sz=(*unpacked_sz);
		snp=snsd_unpack_prio(buf, pack_sz-usz, unpacked_sz);
		if(!snp)
			return 0;

		/* tmp_sz=how much we've read so far from `buf' */
		tmp_sz=(*unpacked_sz)-tmp_sz;	
		buf+=tmp_sz;
		usz+=tmp_sz;

		clist_add(&snp_head, &tmp_counter, snp);
	}

	(*unpacked_sz)+=usz;
	return snp_head;
}

/*
 * snsd_pack_service
 *
 * It packs the `service' snsd_service struct in the `pack' buffer, which has 
 * `free_sz' bytes allocated.
 * In the packs it includes the `service'->prio llist too.
 *
 * On error -1 is returned, otherwise the size of the package is returned.
 */
int snsd_pack_service(char *pack, size_t free_sz, snsd_service *service)
{
	char *buf=pack;
	int wsz=0, sz=0;

	if(free_sz < SNSD_SERVICE_PACK_SZ)
		return -1;
	
	(*(u_short *)(buf))=htons(service->service);
	buf+=sizeof(short);
	
	(*(u_char *)(buf))=service->proto;
	buf+=sizeof(u_char);
	
	wsz+=SNSD_SERVICE_PACK_SZ;
	
	sz=snsd_pack_all_prios(buf, free_sz-wsz, service->prio);
	if(sz <= 0)
		return -1;
	wsz+=sz;
	
	return wsz;
}

/*
 * snsd_unpack_service
 *
 * It unpacks a packed snsd_service struct and returns it.
 * `pack' is the package, which is `pack_sz' big.
 *
 * in the service->prio->node llist.
 *
 * `*unpacked_sz' is incremented by the number of unpacked bytes.
 * 
 * On error 0 is returned
 */
snsd_service *snsd_unpack_service(char *pack, size_t pack_sz, size_t *unpacked_sz)
{
	snsd_service *sns;

	sns=xmalloc(sizeof(snsd_service));
	setzero(sns, snsd_service);
	
	sns->service=ntohs((*(u_short *)pack));
	pack+=sizeof(short);
	
	sns->proto=(*(u_char *)pack);
	pack+=sizeof(u_char);
	
	(*unpacked_sz)+=SNSD_SERVICE_PACK_SZ;

	pack_sz-=SNSD_SERVICE_PACK_SZ;
	sns->prio=snsd_unpack_all_prios(pack, pack_sz, unpacked_sz);
	if(!sns->prio)
		return 0;

	return sns;
}

/*
 * snsd_pack_all_services
 *
 * It packs the whole snsd_service linked list whose head is `head'.
 * `pack' is the buffer where the package will be stored, it must have already
 * `pack_sz' bytes allocated.
 * Use SNSD_SERVICE_PACK_SZ(head) to calculate the pack size.
 *
 * The number of bytes stored in `pack' is returned.
 *
 * On error -1 is returned.
 */
int snsd_pack_all_services(char *pack, size_t pack_sz, snsd_service *head)
{
	struct snsd_service_llist_hdr *hdr;
	snsd_service *sns=head;
	int sz=0, wsz=0, counter;

	hdr=(struct snsd_service_llist_hdr *)pack;
	pack+=sizeof(struct snsd_service_llist_hdr);
	wsz+=sizeof(struct snsd_service_llist_hdr);
	
	list_for(sns) {
		sz=snsd_pack_service(pack, pack_sz-wsz, sns);
		if(sz <= 0)
			return -1;
		
		wsz+=sz; pack+=sz; counter++;
	}
	
	hdr->count=htons(counter);
	return wsz;
}

/*
 * snsd_unpack_all_service
 *
 * It unpacks the packed snsd_service llist.
 * The head of the newly allocated llist is returned.
 * in the service->node linked lists
 *
 * `*unpacked_sz' is incremented by the number of unpacked bytes.
 *
 * On error 0 is returned.
 */
snsd_service *snsd_unpack_all_service(char *pack, size_t pack_sz, size_t *unpacked_sz)
{
	snsd_service *sns_head=0, *sns=0;
	char *buf=pack;
	int i, sz=0, tmp_sz, usz=0, counter, tmp_counter;
	
#define INC_SZ_AND_CHECK_OVERFLOW(inc) {				\
	sz+=(inc);							\
	if(sz >= pack_sz) return 0;					\
}
	INC_SZ_AND_CHECK_OVERFLOW(sizeof(struct snsd_service_llist_hdr));
	
	counter=ntohs((*(short *)buf));
	buf+=sizeof(short);
	usz+=sizeof(short);

	if(counter > SNSD_MAX_RECORDS)
		return 0;
	
	for(i=0; i<counter; i++) {
		INC_SZ_AND_CHECK_OVERFLOW(SNSD_SERVICE_PACK_SZ);
		
		tmp_sz=(*unpacked_sz);
		sns=snsd_unpack_service(buf, pack_sz-usz, unpacked_sz);
		if(!sns)
			return 0;

		/* tmp_sz=how much we've read from `buf' */
		tmp_sz=(*unpacked_sz)-tmp_sz;	
		buf+=tmp_sz;
		usz+=tmp_sz;

		clist_add(&sns_head, &tmp_counter, sns);
	}

	(*unpacked_sz)+=usz;
	return sns_head;
}

/*
 *
 *   *  *  *  Misc functions  *  *  *
 *   
 */

int snsd_count_nodes(snsd_service *head)
{
	int count=0;
	
	list_for(head)
		list_for(head->prio)
			count+=list_count(head->prio->node);
	return count;
}

/*
 * snsd_choose_wrand
 *
 * It returns a snsd_node of the `head' llist. The snsd_node is chosen
 * randomly. The weight of a node is proportional to its probability of being
 * picked.
 * On error (no nodes?) 0 is returned.
 */
snsd_node *snsd_choose_wrand(snsd_node *head)
{
	snsd_node *snd=head;
	int tot_w=0, r=0, nmemb=0;

	nmemb=list_count(snd);
	list_for(snd)
		tot_w+=snd->weight;

	if(!tot_w)
		return list_pos(snd, rand_range(0, nmemb-1));
		
	r=rand_range(1, tot_w);

	tot_w=0; snd=head;
	list_for(snd) {
		if(r > tot_w && (r <= tot_w+snd->weight))
			return snd;
		tot_w+=snd->weight;
	}
	
	return 0;
}

/*
 * snsd_highest_prio
 *
 * It returns the snsd_prio struct which has the highest `prio' value.
 */
snsd_prio *snsd_highest_prio(snsd_prio *head)
{
	snsd_prio *highest=head;

	list_for(head)
		if(head->prio > highest->prio)
			highest=head;

	return highest;
}
