#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <regex.h>
#include <getopt.h>
#include <libintl.h>
#include <locale.h>
#include <time.h>
#include <libintl.h>
#include <locale.h>
#include "openssl_sign.h"
#include "define.h"
#include "sender.h"
#include "openssl_sign.h"

/* Howto adding new fields:
	1. Add defines for desired new field in define.h
	2. Add desired options in mergedlanbeaconCreator()
*/

// code loosely based on code from my Systempraktikum https://github.com/ciil/nine-mens-morris/blob/master/src/config.c
char *mergedlanbeaconCreator (int *argc, char **argv, int *lldpdu_len, 
								struct open_ssl_keys *lanbeacon_keys) {

	char *mylanbeacon = malloc(1500);
	if(!mylanbeacon) puts(_("malloc error of \"mylanbeacon\" in mergedlanbeaconCreator"));
	//counter for current position in Array combinedBeacon, starting after TLV header
	int currentByte = 0;	

	// Maximum of 5 strings of combined human-readable text in case 
	//they are longer than 507 bytes (TLV max)
	char *combinedString[5];	
	for(int i=0; i<5; i++) {
		combinedString[i] = calloc(507, 1);
		if(!combinedString[i])
			puts(_("malloc error of \"combinedString\" in mergedlanbeaconCreator"));
	}

	// Fill chassis and port subtype with FFs, will be changed to MAC-addresses in send function
	unsigned char chasisSubtype[9] = { 0x02, 0x07, 0x04, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	memcpy(&mylanbeacon[currentByte], chasisSubtype, 9);
	currentByte += 9;
	unsigned char portSubtype[9] = { 0x04, 0x07, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	memcpy(&mylanbeacon[currentByte], portSubtype, 9);
	currentByte += 9;
	// 20 sec time to live
	unsigned char timeToLive[4] = { 0x06, 0x02, 0x00, 0x14 };
	memcpy(&mylanbeacon[currentByte], timeToLive, 4);
	currentByte += 4;

	// custom TLV arguments
	if(*argc == 1) printHelp();
	int opt;
	while((opt=getopt(*argc, argv, "i:n:c:4:6:e:d:r:s:v:p:h")) != -1) {
		switch(opt) {

			case 'i':
				transferToCombinedString (DESCRIPTOR_VLAN_ID, combinedString, optarg);
				unsigned short int vlan_id = htons( (unsigned short int) strtoul(optarg,NULL,10) );
				transferToCombinedBeacon (SUBTYPE_VLAN_ID, &vlan_id, mylanbeacon, &currentByte, 2);
				break;

			case 'n':
				transferToCombinedBeaconAndString(SUBTYPE_NAME, DESCRIPTOR_NAME,
					combinedString, optarg, mylanbeacon, &currentByte);
				break;

			case 'c':
				transferToCombinedBeaconAndString(SUBTYPE_CUSTOM, DESCRIPTOR_CUSTOM,
					combinedString, optarg, mylanbeacon, &currentByte);
				break;

			case '4':
				ipParser (AF_INET, optarg, combinedString, mylanbeacon, &currentByte);
				break;

			case '6':
				ipParser (AF_INET6, optarg, combinedString, mylanbeacon, &currentByte);
				break;

			case 'e':
				;
				regex_t compiled_regex;
				regcomp(&compiled_regex,
					// Pure email address
					"(^[a-zA-Z0-9][a-zA-Z0-9_.]+[a-zA-Z0-9]+"
					"@[a-zA-Z0-9_]+(\\.[a-zA-Z]{2,})+\\.?$)"
					// Or alternatively: any text and then email in brackets
					"|.*<"
					"([a-zA-Z0-9][a-zA-Z0-9_.]+[a-zA-Z0-9]+"
					"@[a-zA-Z0-9_]+(\\.[a-zA-Z]{2,})+\\.?)"
					">",
					REG_EXTENDED);
				if (regexec(&compiled_regex, optarg, 0, NULL, 0) == REG_NOMATCH)
					puts(_("There is an error in the passed email-address. "));
				regfree(&compiled_regex);

				transferToCombinedBeaconAndString(SUBTYPE_EMAIL, DESCRIPTOR_EMAIL,
					combinedString, optarg, mylanbeacon, &currentByte);
				break;

			case 's':
				if (strlen(optarg) > KEY_PATHLENGTH_MAX) {
					puts(_("Passed path to signing key too long. Exiting."));
				}

				strncpy(lanbeacon_keys->path_To_Signing_Key, optarg, KEY_PATHLENGTH_MAX);
				lanbeacon_keys->
					path_To_Signing_Key[strlen(optarg)] = 0;
				break;

			case 'v':
				puts("test");
				if (strlen(optarg) > KEY_PATHLENGTH_MAX) {
					puts(_("Passed path to verifying key too long. Exiting."));
					exit(EXIT_FAILURE);
				}
				strncpy(
					lanbeacon_keys->path_To_Verifying_Key,optarg, KEY_PATHLENGTH_MAX
				);
				lanbeacon_keys->path_To_Verifying_Key[strlen(optarg)] = 0;
				break;

			case 'p':
				if (strlen(optarg) > 256) {
					puts(_("Length of passed password too long. Exiting"));
					exit(EXIT_FAILURE);
				}
				strncpy(lanbeacon_keys->pcszPassphrase, optarg, 256);
				lanbeacon_keys->pcszPassphrase[strlen(optarg)] = 0;
				break;

			case 'd':
				transferToCombinedBeaconAndString(SUBTYPE_DHCP, DESCRIPTOR_DHCP,
					combinedString, optarg, mylanbeacon, &currentByte);
				break;

			case 'r':
				transferToCombinedBeaconAndString(SUBTYPE_ROUTER, DESCRIPTOR_ROUTER,
					combinedString, optarg, mylanbeacon, &currentByte);
				break;

			case 'h':
				printHelp();
				break;

			default:
				printHelp();
		}
	}

	// transfer combined strings to TLVs, each with a maximum size of 507 byte
	for(int i = 0; i < 5; i++) {
		if (0 < strlen(combinedString[i]))
			transferToCombinedBeacon(SUBTYPE_COMBINED_STRING, combinedString [i], 
				mylanbeacon, &currentByte, strlen(combinedString [i]));
	}

	// add signature
	long challenge = 0;
	time_t timeStamp = time(NULL);

	challenge = htonl(challenge);
	memcpy(&mylanbeacon[currentByte], &challenge, 4);
	timeStamp = htonl(timeStamp);
	memcpy(&mylanbeacon[currentByte+4], &timeStamp, 4);
	// add TLV-header using function, this overrides lanbeacon signature part with itself
	transferToCombinedBeacon (SUBTYPE_SIGNATURE, &mylanbeacon[currentByte], 
		mylanbeacon, &currentByte, 256 + 8);

	unsigned char* sig = NULL;
	size_t slen = 0;
	signlanbeacon(&sig, &slen, (const unsigned char *) mylanbeacon, 
		(size_t) currentByte - 256, lanbeacon_keys);
	memcpy(&mylanbeacon[currentByte-256], sig, slen);

	*lldpdu_len = currentByte;

	free(sig);

	return mylanbeacon;
}




// shortcut for cases in which only a string is transferred
void transferToCombinedBeaconAndString (unsigned char subtype, char *TLVdescription,
	char **combinedString, char *source, char *combinedBeacon, int *currentByte) {
	transferToCombinedBeacon (subtype, source, combinedBeacon, currentByte, strlen(source));
	if (!(combinedString == NULL))
		transferToCombinedString (TLVdescription, combinedString, source);
}

// transferring the content of the field to the combined lanbeacon in binary format
void transferToCombinedBeacon ( unsigned char subtype, void *source, 
								char *combinedBeacon, int *currentByte, 
								unsigned short int currentTLVlength) {
	
	// calculating TLV length without header, then combining TLV Header and 
	// transfering combined TLV Header to combined Beacon
	if (1500 < (currentTLVlength + *currentByte + 6)) {
		puts(_("Maximum of 1500 Bytes in LLDP-Packet exceeded, not all information "
			"will be transmitted. Please include less information."));
		return;
	}
	if (currentTLVlength > 507) {
		printf(_("Warning, TLV is %i Bytes long, exceeding maximum of 507 characters "
			"in String. End of string will be cut off."), currentTLVlength);
		currentTLVlength = 507;
	}

	// Shift der bits nach Rechts und anschließendes bitweises OR 
	// zur Kombination der 7+9 bit für subtype und Länge
	unsigned short int TLVheader_combined = htons( (127 * 0b1000000000) | (currentTLVlength+4) );
	memcpy (&combinedBeacon[*currentByte], &TLVheader_combined, 2);

	// transfer OUI and OUI subtype to combined Beacon
	// first two bits 11 have to be left like this
	// REF: nach http://standards.ieee.org/getieee802/download/802-2014.pdf clause 9.3
	combinedBeacon[*currentByte+2] = 'L' | 0b10000000;	
	combinedBeacon[*currentByte+3] = 'M' ;
	combinedBeacon[*currentByte+4] = 'U' ;
	combinedBeacon[*currentByte+5] = subtype;

	// transfer information to combinedBeacon
	memcpy(&combinedBeacon[*currentByte+6], source, currentTLVlength);
	*currentByte = *currentByte + 6 + currentTLVlength;
}

// transferring the content of the field to the combined string in human-readable format
// if one combined string exceeds 507 byte limit of TLV it is put to the next combined string TLV
void transferToCombinedString (char *TLVdescription, char **combinedString, char *TLVcontents) {
	int stringToBeFilled;
	if (507 < (strlen(TLVdescription) + strlen(TLVcontents) + 2 ) ) {
		printf(_("String: %s is too long to be included as text and will be skipped\n"), TLVcontents);
		return;
	}

	for(stringToBeFilled = 0; stringToBeFilled < 5; stringToBeFilled++) {
		if (507 > strlen( combinedString[stringToBeFilled]) 
			+ strlen(TLVdescription) + strlen(TLVcontents) + 2)
				break;
	}
	snprintf(&combinedString[stringToBeFilled][strlen( combinedString[stringToBeFilled])] ,
		strlen(TLVdescription) + strlen(TLVcontents) + 2 + 2,
		"%s%s%s%s", TLVdescription, " ", TLVcontents, ". " );
}

// using regex to get IP-addresses from string input, then 
// convert them to binary representation for transport
void ipParser (int ip_V4or6, char *optarg, char **combinedString, char *mylanbeacon, int *currentByte) {

	int IP_binaryLength = (ip_V4or6 == AF_INET) ? 5 : 17;
	size_t ip_strlen = (ip_V4or6 == AF_INET) ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN;

	char foundAddress[ip_strlen];
	char ip_AddressesInBinary[200] = "";

	// initializing to 0 to support first run of loop, 
	// since it expects some results from previous run
	int gefundeneIPAdressenAnzahl = 0, endOfLastString = 0;
	regex_t compiled_regex;
	regmatch_t regex_matches_pointer[3];
	regex_matches_pointer[0].rm_so = regex_matches_pointer[0].rm_eo  = 0;

	// using regex to get IP-addresses from string input
	regcomp(&compiled_regex, ip_V4or6 == AF_INET ?
		"([0-9.]{7,15})\\/([0-9]{1,2})"
		: "([a-fA-F0-9:]{4,45})\\/([0-9]{1,3})", REG_EXTENDED);

	while (1) {	// look for max 10 addresses
		endOfLastString += regex_matches_pointer[0].rm_eo;

		if ( gefundeneIPAdressenAnzahl < 10 &&
			!regexec(&compiled_regex, &optarg[endOfLastString], 3, regex_matches_pointer, 0)) {
				// convert found IP address to binary format
				memset(foundAddress, 0, ip_strlen);
				strncpy (foundAddress,
						&optarg[endOfLastString+regex_matches_pointer[1].rm_so],
						regex_matches_pointer[1].rm_eo - regex_matches_pointer[1].rm_so);
				if (1 != inet_pton(ip_V4or6, foundAddress,
					&ip_AddressesInBinary[gefundeneIPAdressenAnzahl*IP_binaryLength]))
						printf(_("Error Parsing IP-address: %s\n"), optarg);

				// put subnet as last byte of IP-tuple
				memset(foundAddress, 0, ip_strlen);
				strncpy (foundAddress,
						&optarg[endOfLastString+regex_matches_pointer[2].rm_so],
						regex_matches_pointer[2].rm_eo - regex_matches_pointer[2].rm_so);
				ip_AddressesInBinary[gefundeneIPAdressenAnzahl*IP_binaryLength+IP_binaryLength-1]
					= (unsigned char) strtoul(foundAddress,NULL,10);

				gefundeneIPAdressenAnzahl++;
		}

		else break;
	}

	// transfer raw string to combined string TLV and put placeholder for binary representation
	transferToCombinedString (ip_V4or6 == AF_INET ? DESCRIPTOR_IPV4 : DESCRIPTOR_IPV6,
		combinedString, optarg);
	transferToCombinedBeacon (ip_V4or6 == AF_INET ? SUBTYPE_IPV4 : SUBTYPE_IPV6,
		ip_AddressesInBinary, mylanbeacon, currentByte, gefundeneIPAdressenAnzahl*IP_binaryLength);

	if (gefundeneIPAdressenAnzahl < 1) {
		printf(_("Exiting since no valid IP networks (format e.g. 192.168.178.1/24) "
			"could be found in provided string \"%s\"."), optarg);
		exit(EXIT_FAILURE);
	}

	regfree(&compiled_regex);
	return;
}


void printHelp() {
	printf( "%s%s", _("Usage: "),
		"\t./lanbeacon [-i vlan_id] [-n VLAN_NAME] [-4 IPv4_SUBNETWORK] [-6 IPv6_SUBNETWORK] "
		"[-e EMAIL_CONTACTPERSON] [-d DHCP_TYPES] [-r ROUTER_INFORMATION] [-c CUSTOM_STRING]\n");
	printf("\t./client -r\n");
	printf("\t./client -h\n");
	exit(EXIT_FAILURE);
}


