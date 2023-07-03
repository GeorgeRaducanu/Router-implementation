// Copyright Raducanu George-Cristian 2022-2023 321CAb
#include "include/queue.h"
#include "include/lib.h"
#include "include/protocols.h"
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <aio.h>

// declar ca variabile locale tabelele si dimensiunile lor
// pentru a nu ma complica in anumite functii cu parametrii extra

// pt tabela de rutare
struct route_table_entry *rtable;
int rtable_len;

struct arp_entry *my_mac_table;
int my_mac_len;

int cmp(const void *e1, const void *e2) {
	
	struct route_table_entry r1 = *(struct route_table_entry *)e1;
	struct route_table_entry r2 = *(struct route_table_entry *)e2;
	// convertire la entry-uri in tabela de rutare prin pointeri
	// dupa valoarea reala a prefixului
	// si nu trebuie uitat de ntohl pt a schimba in network order

	if (ntohl(r1.prefix & r1.mask) > ntohl(r2.prefix & r2.mask))
		return 1;
	else if (ntohl(r1.prefix & r1.mask) < ntohl(r2.prefix & r2.mask))
		return -1;

	// altfel in caz de egalitate dupa dimensiunea masti
	if (ntohl(r1.mask) > ntohl(r2.mask))
		return 1;
	return -1;
}

struct route_table_entry *get_best_r(u_int32_t ip_dest) {
	// aici o cautare binara (mai eficienta) in vectorul ce a fost sortat in main
	// complexitate O(log n)
	int st = 0, dr = rtable_len - 1, mij;
	struct route_table_entry *entr = NULL;
	while (st <= dr) {
		mij = (dr - st) / 2 + st;
		if (ntohl(rtable[mij].prefix & rtable[mij].mask) < ntohl(ip_dest & rtable[mij].mask)) {
			st = mij + 1;
		} else if (ntohl(rtable[mij].prefix & rtable[mij].mask) > ntohl(ip_dest & rtable[mij].mask)) {
			dr = mij - 1;
		} else {
			// am gasit match si mai caut in dreapta poate gasesc unul mai bun
			entr = &(rtable[mij]);
			st = mij + 1;
		}
	}
	return entr;
}


struct arp_entry *get_my_arp_entry(uint32_t given_ip) {
	// cautare simpla liniara in tabela
	// Complexitate O(n)
	for (int i = 0; i < my_mac_len; ++i) {
		if (my_mac_table[i].ip == given_ip) {
			return &(my_mac_table[i]);
		}
	}
	return NULL;
}

// Structura ajutatoare pentru a stoca un pachet si dimensiunea lui
// intrucat in coada oferita pot tine doar un void * si tin pointeri
// catre astfel de structuri
struct payload {
	char info[MAX_PACKET_LEN];
	size_t dimens;
};

int main(int argc, char *argv[])
{
	char buf[MAX_PACKET_LEN];

	// Aici o sa aloc tabelele
	// le aloc dinamic pentru a putea aloca o dimensiune mai mares fara restrictii
	// de memorie
	rtable = malloc(sizeof(struct route_table_entry) * 80000);
	my_mac_table = malloc(sizeof(struct arp_entry) * 200);
	// citesc tabela de rutare prin paramentrul primit la intrare
	rtable_len = read_rtable(argv[1], rtable);

	// O optimizare pentru a cauta mai rapid next hop utilizand cautarea binara
	// O idee simpla ce are complexitatea O(n * log n) este de a sorta vectorul
	// crescator dupa prefixul lui real si ca departajare in cax de egalitate 
	// crescator dupa lungimea mastii
	// Ca sa intre in timp si sa fie eficient
	// cautarea binara solutie nu la fel de eficienta dar mai eleganta ca trie-ul 
	// din punct de vedere al implementarii si intelegerii
	qsort(rtable, rtable_len, sizeof(struct route_table_entry), cmp);

	// coada mea ce va retine pachetele ce nu le pot trimite inca
	// actualy pointeri catre structuri create de mine
	queue coada = queue_create();

	// Do not modify this line
	init(argc - 2, argv + 2);

	while (1) {
		int interface;
		size_t len;

		interface = recv_from_any_link(buf, &len);
		DIE(interface < 0, "recv_from_any_links");

		struct ether_header *eth_hdr = (struct ether_header *) buf;
		/* Note that packets received are in network order,
		any header field which has more than 1 byte will need to be conerted to
		host order. For example, ntohs(eth_hdr->ether_type). The oposite is needed when
		sending a packet on the link, */

		if (ntohs(eth_hdr->ether_type) == 0x0800) {
			// aici tratez partea de ipv4
			// 0x800 pachet tip ipv4

			// pointer catre headerul de ipv4
			struct iphdr *ip_hdr = (struct iphdr *)(buf + sizeof(struct ether_header));

			// urmatoarea parte este asemanatoare cu labul4 implementat de mine
			// cu imbunatatiri si cerinte extra

			// stochez checksum-ul vechi, il calculez din nou si astfel e verificata integritatea
			uint16_t old_checksum = ntohs(ip_hdr->check);
			ip_hdr->check = 0;
			uint16_t new_checksum = checksum((uint16_t *)ip_hdr, sizeof(struct iphdr));
			if (old_checksum != new_checksum) {
				printf("Bad checksum!");
				continue;
			}

			// in continuare trebuie sa verificam ttl ul pachetului
			if (ip_hdr->ttl <= 1) {
				// trebuie sa trimit icmp pt ttl fiindca ttl <= 1
				// noul pachet zeroizat + pointeri catre headerele noului pachet raspuns
				char new_buf[MAX_PACKET_LEN] = {0};
				struct ether_header *eth_hdr_new = (struct ether_header *)new_buf;
				struct iphdr *ip_hdr_new = (struct iphdr *)(new_buf + sizeof(struct ether_header));
				struct icmphdr *icmp_new = (struct icmphdr *)(new_buf + sizeof(struct ether_header) + sizeof(struct iphdr));
				
				// completam in noul pachet noul header ethernet cu adrese mac coresp si type
				memcpy(eth_hdr_new->ether_dhost, eth_hdr->ether_shost, 6);
				uint8_t mac_aux[6];
				get_interface_mac(interface, mac_aux);
				memcpy(eth_hdr_new->ether_shost, mac_aux, 6);
				eth_hdr_new->ether_type = htons(0x800);

				// acuma completez header-ul ip pt noul pachet
				// la baza noul pachet dar fac si schimbari in campuri
				// unde este necesar
				memcpy(ip_hdr_new, ip_hdr, sizeof(struct iphdr));
				ip_hdr_new->protocol = 1; // pentru protocol
				ip_hdr_new->tot_len = htons(sizeof(struct iphdr) + sizeof(struct icmphdr) + 8 + sizeof(struct iphdr));
				ip_hdr_new->ttl = 64;
				ip_hdr_new->saddr = ip_hdr->daddr;
				ip_hdr_new->daddr = ip_hdr->saddr;
				// checksum ul pt iphdr nou
				ip_hdr_new->check = 0;
				ip_hdr_new->check = htons(checksum((uint16_t *)ip_hdr_new, sizeof(struct iphdr)));

				// pun codurile corespunatoare pt icmp
				icmp_new->type = 11;
				icmp_new->code = 0;
				// acuma pun si checksum ul pt icmp
				icmp_new->checksum = 0;
				icmp_new->checksum = htons(checksum((uint16_t *)icmp_new, sizeof(struct icmphdr)));

				// mai pun dupa icmphdr ce trebuie din buf, inca iphdr vechi + 8 octeti
				char *extra_payload = new_buf + sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct icmphdr);
				memcpy(extra_payload, buf +  sizeof(struct ether_header), sizeof(struct iphdr) + 8);
				// o variabila in care stochez ajutator noua lungime a pachetului
				size_t new_len;
				new_len = sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct icmphdr) + 8 + sizeof(struct iphdr);
				send_to_link(interface, new_buf, new_len);
				continue;

			} else {
				// decrementez ttl-ul si recalculez suma de control
				ip_hdr->ttl--;
				ip_hdr->check = 0;
				uint16_t my_checksum = checksum((uint16_t *) ip_hdr, sizeof(struct iphdr));
				ip_hdr->check = htons(my_checksum);

				// acum verificam si daca e de tip echo request ca sa trimitem echo reply
				if (ip_hdr->daddr == inet_addr(get_interface_ip(interface))) {
					char new_buf[MAX_PACKET_LEN] = {0};
					// pointeri catre antetele din noul pachet
					struct ether_header *eth_hdr_new = (struct ether_header *)new_buf;
					struct iphdr *ip_hdr_new = (struct iphdr *)(new_buf + sizeof(struct ether_header));
					struct icmphdr *icmp_new = (struct icmphdr *)(new_buf + sizeof(struct ether_header) + sizeof(struct iphdr));

					// completam noul header ethernet folosindu-ma de cel vechi
					memcpy(eth_hdr_new->ether_dhost, eth_hdr->ether_shost, 6);
					uint8_t mac_aux[6];
					get_interface_mac(interface, mac_aux);
					memcpy(eth_hdr_new->ether_shost, mac_aux, 6);
					eth_hdr_new->ether_type = htons(0x800); // pt ipv4

					// headerul de tip ip e ala vechi
					memcpy(ip_hdr_new, ip_hdr, sizeof(struct iphdr));
					ip_hdr_new->protocol = 1;
					// tot 1 la protocol pt ca e legat de icmp
					// noul size al ip ului este iphdr + icmphdr + pachetul vechi
					ip_hdr_new->tot_len = htons(sizeof(struct iphdr) + sizeof(struct icmphdr) + len);
					ip_hdr_new->ttl = 64;
					// aici ip->dest si ip->src din ala vechi se inverseaza
					// pentru a deveni parte din noul ip
					// se inverseaza rolurile
					ip_hdr_new->saddr = ip_hdr->daddr;
					ip_hdr_new->daddr = ip_hdr->saddr;
					// normal ca trebuie sa recalculam si checksum ul pt a verifica apoi integritatea
					ip_hdr_new->check = 0;
					ip_hdr_new->check = htons(checksum((uint16_t *)ip_hdr_new, sizeof(struct iphdr)));
					// acum ne ocupam de noul header icmp
					icmp_new->type = 0;
					icmp_new->code = 0;
					// acum calculam checksum-ul pt icmp
					icmp_new->checksum = 0;
					icmp_new->checksum = htons(checksum((uint16_t *)icmp_new, sizeof(struct icmphdr)));

					// payload ce se adauga => de fapt intregul vechi pachet
					char *extra_payload = new_buf + sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct icmphdr);
					
					// new_len1 de fapt lungimea a ce contine pachetul vechi dupa headerul icmp
					size_t new_len1 = len - (sizeof(struct ether_header)+ sizeof(struct iphdr) + sizeof(struct icmphdr));
					memcpy(extra_payload, buf +  sizeof(struct ether_header)+ sizeof(struct iphdr) + sizeof(struct icmphdr),
							new_len1);
					// apelam send acuma cu lg pachetului vechi
					send_to_link(interface, new_buf, len);
					continue;
				}
			}
			// cautam best route si aici intervine functia noastra eficienta
			struct route_table_entry *best_router = get_best_r(ip_hdr->daddr);
			if (best_router == NULL) {
				// trebuie sa trimit icmp pt destination unreachable ca nu stiu pe unde sa o iau
				// noul pachet & pointeri catre headerele de structuri
				char new_buf[MAX_PACKET_LEN] = {0};
				struct ether_header *eth_hdr_new = (struct ether_header *)new_buf;
				struct iphdr *ip_hdr_new = (struct iphdr *)(new_buf + sizeof(struct ether_header));
				struct icmphdr *icmp_new = (struct icmphdr *)(new_buf + sizeof(struct ether_header) + sizeof(struct iphdr));
				
				// umplem noul header ethernet ajutandu-ne de cel vechi
				memcpy(eth_hdr_new->ether_dhost, eth_hdr->ether_shost, 6);
				// gasim mac-ul nostru
				uint8_t mac_aux[6];
				get_interface_mac(interface, mac_aux);
				memcpy(eth_hdr_new->ether_shost, mac_aux, 6);
				eth_hdr_new->ether_type = htons(0x800);

				// acuma completez noul ip si ma folosesc de ala vechi
				memcpy(ip_hdr_new, ip_hdr, sizeof(struct iphdr));
				ip_hdr_new->protocol = 1; // esential criteriu de a ne da seama de tip
				ip_hdr_new->tot_len = htons(sizeof(struct iphdr) + sizeof(struct icmphdr) + 8 + sizeof(struct iphdr));
				ip_hdr_new->ttl = 64;
				ip_hdr_new->saddr = ip_hdr->daddr;
				ip_hdr_new->daddr = ip_hdr->saddr;
				ip_hdr_new->check = 0;
				ip_hdr_new->check = htons(checksum((uint16_t *)ip_hdr_new, sizeof(struct iphdr)));

				// pun codurile corespunatoare pt icmp pt dest unreacheble
				icmp_new->type = 3;
				icmp_new->code = 0;
				// acuma pun si checksum ul pt icmp
				icmp_new->checksum = 0;
				icmp_new->checksum = htons(checksum((uint16_t *)icmp_new, sizeof(struct icmphdr)));

				// identic cu trimiterea de la ttl <= 1
				// in esenta acelasi procedeu doar ca alt cod in icmp header
				char *extra_payload = new_buf + sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct icmphdr);
				memcpy(extra_payload, buf +  sizeof(struct ether_header), sizeof(struct iphdr) + 8);
				size_t new_len;
				new_len = sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct icmphdr) + 8 + sizeof(struct iphdr);
				send_to_link(interface, new_buf, new_len);
				continue;
			}

			// Caut in arp-ul meu
			// Tabela asta pe care o construiesc eu pe parcurs
			struct arp_entry *mac_entry = get_my_arp_entry(best_router->next_hop);
			if (mac_entry == NULL) {
				// trebuie sa fac request
				// mai intai imi umplu struct ul meu pentru a il baga in coada
				// intrucat in coada pot retine doar void * asa ca retin adresa catre
				// struct ul meu
				// am nevoie si de len nu doar de pachet pentru ca dupa e greu de determinata lg
				struct payload pack; // static alocat ca sa nu a complic cu memoria
				pack.dimens = len;
				// copiere in pack.info a intregului meu pachet
				memcpy(pack.info, buf, len);

				// am bagat in coada mea adresa catre struct
				queue_enq(coada, &pack);

				// creez request ul si il trimit
				char my_request[MAX_PACKET_LEN] = {0};
				struct ether_header *eth_arp = (struct ether_header *)my_request;
				struct arp_header *my_arp_repl = (struct arp_header *)(my_request + sizeof(struct ether_header));

				// completez header-ul ether
				memset(eth_arp->ether_dhost, 255, 6);
				uint8_t mac_helper[6] = {0};
				// mac ul next hop ului
				get_interface_mac(best_router->interface, mac_helper);
				memcpy(eth_arp->ether_shost, mac_helper, 6);
				eth_arp->ether_type = htons(0x806); // de tip arp

				my_arp_repl->hlen = 6;
				my_arp_repl->htype = htons(1);
				my_arp_repl->op = htons(1); // fac request
				my_arp_repl->plen = 4;
				// format pt prototcol
				my_arp_repl->ptype = htons(0x800);
				memcpy(my_arp_repl->sha, mac_helper, 6);
				memset(my_arp_repl->tha, 0, 6);

				// setez ip urile
				my_arp_repl->tpa = best_router->next_hop;
				my_arp_repl->spa = inet_addr(get_interface_ip(best_router->interface));

				// trimit
				send_to_link(best_router->interface, my_request, sizeof(struct ether_header) + sizeof(struct arp_header));
				continue;
			}

			memcpy(eth_hdr->ether_dhost, mac_entry->mac, 6);
			get_interface_mac(best_router->interface, eth_hdr->ether_shost);
			send_to_link(best_router->interface, buf, len);
			continue;

		} else if (ntohs(eth_hdr->ether_type) == 0x0806) {
			// aici partea de arp
			// pt arp tipul este 0x806
			struct arp_header *my_arp = (struct arp_header *)(buf + sizeof(struct ether_header));
			if (my_arp->tpa != inet_addr(get_interface_ip(interface))) {
				struct route_table_entry* best_r;
				best_r = get_best_r(my_arp->tpa);
				if (!best_r)
					continue;
				send_to_link(best_r->interface, buf, len);
				continue;
			}
			if (htons(my_arp->op) == 1) {
				// primesc un request si fac reply
				// si spun cine sunt eu
				// pachetul pe care il trimit
				char my_reply[MAX_PACKET_LEN] = {0};
				struct ether_header *eth_arp = (struct ether_header *)my_reply;
				struct arp_header *my_arp_repl = (struct arp_header *)(my_reply + sizeof(struct ether_header));

				// completez header-ul ether ca sa compun pachetul
				memcpy(eth_arp->ether_dhost, eth_hdr->ether_shost, 6);
				uint8_t mac_helper[6];
				get_interface_mac(interface, mac_helper);
				memcpy(eth_arp->ether_shost, mac_helper, 6);
				eth_arp->ether_type = htons(0x806);

				// acum partea de arp
				my_arp_repl->hlen = 6;
				my_arp_repl->htype = htons(1);
				my_arp_repl->op = htons(2);
				my_arp_repl->plen = 4;
				// cod pt ptype
				my_arp_repl->ptype = htons(0x800); // conform cerintei ptype specific IPV4
				memcpy(my_arp_repl->sha, mac_helper, 6);
				memcpy(my_arp_repl->tha, eth_hdr->ether_shost, 6);
				
				// inversez intre astea 2 din arp ul vechi
				my_arp_repl->tpa = my_arp->spa;
				my_arp_repl->spa = my_arp->tpa;
				
				// trimit pe interface, de unde a venit, ii rapsund
				send_to_link(interface, my_reply, sizeof(struct ether_header) + sizeof(struct arp_header));
				continue;
			}
			if (htons(my_arp->op) == 2) {
				// primesc un reply
				// adaug in cache si verific coada de pachete
				// ip in spa
				// mac in sha
				struct arp_entry new_element;
				new_element.ip = my_arp->spa;
				memcpy(new_element.mac, my_arp->sha, 6);

				// adaug efectiv in tabela
				my_mac_table[my_mac_len++] = new_element;

				// acuma parcurg coada
				// pentru a pastra vechea ordine fac si o coada auxiliara
				// ca apoi sa ii atribui celei vechi adresa celei noi pentru a pastra 
				// pachetele
				queue aux_q = queue_create();
				while (!queue_empty(coada)) {
					struct payload *now_pack;
					now_pack = queue_deq(coada);
					char new_pack[MAX_PACKET_LEN] = {0};
					size_t new_len;
					new_len = now_pack->dimens;
					memcpy(new_pack, now_pack->info, new_len);

					struct ether_header *new_eth = (struct ether_header *)new_pack;
					struct iphdr *new_ip = (struct iphdr *) (new_pack + sizeof(struct ether_header));

					struct route_table_entry *best_router2 = get_best_r(new_ip->daddr);
					struct arp_entry *mac_entry2 = get_my_arp_entry(best_router2->next_hop);
					// am gasit pe unde o trimit ?
					if (mac_entry2 == NULL) {
						// ii mai dau enq in auxiliara
						queue_enq(aux_q, now_pack);
					} else {
						// trimit pachetul pe calea gasita
						memcpy(new_eth->ether_dhost, mac_entry2->mac, 6);
						get_interface_mac(best_router2->interface, new_eth->ether_shost);
						send_to_link(best_router2->interface, new_pack, new_len);
					}
					// parcurgem cat timp coada e goala
				}
				coada = aux_q; // interschimb pt a avea in coada mea toate pachetele
			}
		}
	}
	// dealoc tabelele
	free(rtable);
	free(my_mac_table);
	return 0;
}
