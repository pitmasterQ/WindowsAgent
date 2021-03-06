//====================================================================================
// Open Computer and Software Inventory Next Generation
// Copyright (C) 2010 OCS Inventory NG Team. All rights reserved.
// Web: http://www.ocsinventory-ng.org

// This code is open source and may be copied and modified as long as the source
// code is always made freely available.
// Please refer to the General Public Licence V2 http://www.gnu.org/ or Licence.txt
//====================================================================================

// IPHelper.cpp: implementation of the CIPHelper class.
//
//////////////////////////////////////////////////////////////////////


#include "stdafx.h"
#include "NetworkAdapter.h"
#include "NetworkAdapterList.h"
#include "debuglog.h"
#include "IPHelper.h"
#include <Winsock2.h>
#include <Snmp.h>
#include "snmpapi.h"
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <set>
#include <cstring>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CIPHelper::CIPHelper()
{

}

CIPHelper::~CIPHelper()
{

}

BOOL CIPHelper::GetNetworkAdapters(CNetworkAdapterList *pList)
{
	HINSTANCE			hDll;
	DWORD				(WINAPI *lpfnGetAdaptersAddresses)(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES myInfo, DWORD *size);
	DWORD				(WINAPI *lpfnGetIfTable2Ex)(MIB_IF_TABLE_LEVEL Level, PMIB_IF_TABLE2 *pIfTable);
	PMIB_IF_TABLE2		pIfTable;
	PMIB_IF_ROW2		pIfEntry;
	PMIB_IPADDRTABLE	pIPAddrTable;
	DWORD				dwSize = 0,
						size = 0,
						dwSizeBis = 0;
	IN_ADDR				IPAddr, 
						IPAddrBis, 
						ipa;
	PIP_ADAPTER_ADDRESSES	pAdresses, 
							pAdressesBis, 
							pAdapterAddr = NULL,
							pAdapterAddrBis = NULL;
	PIP_ADAPTER_UNICAST_ADDRESS		pUnicast = NULL;
	PIP_ADAPTER_PREFIX	pPrefix = NULL;
	SOCKET_ADDRESS		*pDhcp;
	PIP_ADAPTER_GATEWAY_ADDRESS		pGateway = NULL;
	ULONG				ulLength = 0,
						dwIndex,
						ipAdr,
						ipMsk,
						nbRez;
	UINT				uIndex = 0,
						ifIndex;
	CNetworkAdapter		cAdapter;
	CString				csMAC,
						csAddress,
						csSubnet,
						csSubnetNetwork,
						csAddressIp,
						csDescription,
						csPrefixLength;
	char				str[INET_ADDRSTRLEN],
						bufferstr[INET_ADDRSTRLEN],
						bufferRez[INET_ADDRSTRLEN];
	auto				family = NULL;


	AddLog(_T("IpHlpAPI GetNetworkAdapters...\n"));
	// Reset network adapter list content
	while (!(pList->GetCount() == 0))
		pList->RemoveHead();

	// Load the IpHlpAPI dll and get the addresses of necessary functions
	if ((hDll = LoadLibrary(_T("iphlpapi.dll"))) < (HINSTANCE)HINSTANCE_ERROR)
	{
		// Cannot load IpHlpAPI MIB
		AddLog(_T("IpHlpAPI GetNetworkAdapters: Failed because unable to load <iphlpapi.dll> !\n"));
		hDll = NULL;
		return FALSE;
	}

	if ((*(FARPROC*)&lpfnGetIfTable2Ex = GetProcAddress(hDll, "GetIfTable2Ex")) == NULL)
	{
		// Tell the user that we could not find a usable IpHlpAPI DLL.                                  
		FreeLibrary(hDll);
		AddLog(_T("IpHlpAPI GetNetworkAdapters: Failed because unable to load <iphlpapi.dll> !\n"));
		return FALSE;
	}

	if ((*(FARPROC*)&lpfnGetAdaptersAddresses = GetProcAddress(hDll, "GetAdaptersAddresses")) == NULL)
	{
		// Tell the user that we could not find a usable IpHlpAPI DLL.                                  
		FreeLibrary(hDll);
		AddLog(_T("IpHlpAPI GetNetworkAdapters: Failed because unable to load <iphlpapi.dll> !\n"));
		return FALSE;
	}

	// Call GetIfTable to get memory size
	AddLog(_T("IpHlpAPI GetNetworkAdapters: Calling GetIfTable to determine network adapter properties..."));
	pIfTable = NULL;
	pIfTable = (PMIB_IF_TABLE2)malloc(sizeof(PMIB_IF_TABLE2));
	if (pIfTable == NULL) {
		AddLog(_T("Error allocating memory needed to call GetIfTable\n"));
		return FALSE;
	}

	//call GetIfTable2Ex
	switch (lpfnGetIfTable2Ex(MibIfTableRaw, &pIfTable)) 
	{
	case NO_ERROR:
		break;
	case ERROR_NOT_SUPPORTED:
		FreeMibTable(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because OS does not support GetIfTable2Ex API function !\n"));
		return FALSE;
	case ERROR_BUFFER_OVERFLOW:
	case ERROR_INSUFFICIENT_BUFFER:
		FreeMibTable(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because OS does not support GetIfTable2Ex API function !\n"));
		return FALSE;
	default:
		FreeMibTable(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because unknown error !\n"));
		return FALSE;
	}

	// Call GetAdptersAddresses with length to 0 to get size of required buffer
	AddLog(_T("OK\nIpHlpAPI GetNetworkAdapters: Calling GetAdapterAddresses to determine IP Info..."));
	pAdresses = NULL;
	dwSize = 0;

	switch (lpfnGetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAdresses, &dwSize))
	{
	case NO_ERROR: // No error => no adapters
	case ERROR_NO_DATA:
		FreeMibTable(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because no network adapters !\n"));
		return FALSE;
	case ERROR_NOT_SUPPORTED: // Not supported
		FreeMibTable(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because OS does not support GetAdapterAddresses API function !\n"));
		return FALSE;
	case ERROR_BUFFER_OVERFLOW: // We must allocate memory
		break;
	default:
		FreeMibTable(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because unknown error !\n"));
		return FALSE;
	}

	if ((pAdresses = (PIP_ADAPTER_ADDRESSES)malloc(dwSize + 1)) == NULL)
	{
		FreeMibTable(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because memory error !\n"));
		return FALSE;
	}

	// Recall GetAdptersAddresses
	switch (lpfnGetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAdresses, &dwSize))
	{
	case 0: // No error
		break;
	case ERROR_NO_DATA: // No adapters
		FreeMibTable(pIfTable);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because no network adapters !\n"));
		return FALSE;
	case ERROR_NOT_SUPPORTED: // Not supported
		FreeMibTable(pIfTable);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because OS does not support GetAdapterAddresses API function !\n"));
		return FALSE;
	case ERROR_BUFFER_OVERFLOW: // We have allocated needed memory, but not sufficient
		FreeMibTable(pIfTable);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because memory error !\n"));
		return FALSE;
	default:
		FreeMibTable(pIfTable);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because unknown error !\n"));
		return FALSE;
	}

	// Call GetAdptersAddresses with length to 0 to get size of required buffer
	AddLog(_T("OK\nIpHlpAPI GetNetworkAdapters: Calling GetAdapterAddresses to determine IP Gateway..."));
	pAdressesBis = NULL;

	switch (lpfnGetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, NULL, pAdressesBis, &size))
	{
	case NO_ERROR: // No error => no adapters
	case ERROR_NO_DATA:
		FreeMibTable(pIfTable);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because no network adapters !\n"));
		return FALSE;
	case ERROR_NOT_SUPPORTED: // Not supported
		FreeMibTable(pIfTable);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because OS does not support GetAdapterAddresses API function !\n"));
		return FALSE;
	case ERROR_BUFFER_OVERFLOW: // We must allocate memory
		break;
	default:
		FreeMibTable(pIfTable);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because unknown error !\n"));
		return FALSE;
	}

	if ((pAdressesBis = (PIP_ADAPTER_ADDRESSES)malloc(size + 1)) == NULL)
	{
		FreeMibTable(pIfTable);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because memory error !\n"));
		return FALSE;
	}


	// Recall GetAdptersAddresses
	switch (lpfnGetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, NULL, pAdressesBis, &size))
	{
	case 0: // No error
		break;
	case ERROR_NO_DATA: // No adapters
		FreeMibTable(pIfTable);
		free(pAdressesBis);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because no network adapters !\n"));
		return FALSE;
	case ERROR_NOT_SUPPORTED: // Not supported
		FreeMibTable(pIfTable);
		free(pAdressesBis);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because OS does not support GetAdaptersInfo API function !\n"));
		return FALSE;
	case ERROR_BUFFER_OVERFLOW: // We have allocated needed memory, but not sufficient
		FreeMibTable(pIfTable);
		free(pAdressesBis);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because memory error !\n"));
		return FALSE;
	default:
		FreeMibTable(pIfTable);
		free(pAdressesBis);
		free(pAdresses);
		FreeLibrary(hDll);
		AddLog(_T("Failed because unknown error !\n"));
		return FALSE;
	}

	pIPAddrTable = (MIB_IPADDRTABLE *)malloc(sizeof(MIB_IPADDRTABLE));
	if (pIPAddrTable == NULL)
	{
		FreeMibTable(pIfTable);
		free(pAdresses);
		free(pAdressesBis);
		FreeLibrary(hDll);
		AddLog(_T("Failed because memory error !\n"));
		return FALSE;
	}
	else
	{
		// Make an initial call to GetIpAddrTable to get the
		// necessary size into the dwSize variable
		if (GetIpAddrTable(pIPAddrTable, &dwSizeBis, 0) == ERROR_INSUFFICIENT_BUFFER)
		{
			free(pIPAddrTable);
			pIPAddrTable = (MIB_IPADDRTABLE *)malloc(dwSizeBis);
		}
		if (pIPAddrTable == NULL)
		{
			FreeMibTable(pIfTable);
			free(pAdresses);
			free(pAdressesBis);
			FreeLibrary(hDll);
			AddLog(_T("Memory allocation failed for GetIpAddrTable\n"));
			return FALSE;
		}
	}

	if (GetIpAddrTable(pIPAddrTable, &dwSizeBis, 0) == NO_ERROR)
	{

	}
	else
	{
		if (pIPAddrTable)
			AddLog(_T("Call to GetIpAddrTable failed.\n"));
		free(pIPAddrTable);
		FreeMibTable(pIfTable);
		free(pAdresses);
		free(pAdressesBis);
		FreeLibrary(hDll);
		return FALSE;
	}
	
	WSAData d;
	if (WSAStartup(MAKEWORD(2, 2), &d) != 0) 
	{
	}

	// Call GetIfTable2Ex for each interface
	for (dwIndex = 0; dwIndex < pIfTable->NumEntries; dwIndex++)
	{
		pIfEntry = (MIB_IF_ROW2 *)&(pIfTable->Table[dwIndex]);
		if (pIfEntry->Type != IF_TYPE_SOFTWARE_LOOPBACK)
		{
			//if Network card is disabled
			if (pIfEntry->OperStatus == IfOperStatusDown && pIfEntry->TransmitLinkSpeed != NULL)
			{
				// Get the Index
				cAdapter.SetIfIndex(pIfEntry->InterfaceIndex);
				// Get the type
				cAdapter.SetType(GetAdapterType(pIfEntry->Type));
				// Get the MIB type
				cAdapter.SetTypeMIB(GetIfType(pIfEntry->Type));
				// Get the description;
				cAdapter.SetDescription(pIfEntry->Description);
				// Get MAC Address
				csMAC.Format(_T("%02X:%02X:%02X:%02X:%02X:%02X"),
					pIfEntry->PhysicalAddress[0], pIfEntry->PhysicalAddress[1],
					pIfEntry->PhysicalAddress[2], pIfEntry->PhysicalAddress[3],
					pIfEntry->PhysicalAddress[4], pIfEntry->PhysicalAddress[5]);
				cAdapter.SetMACAddress(csMAC);
				// Get the Speed
				cAdapter.SetSpeed(pIfEntry->TransmitLinkSpeed);
				// Get the status
				cAdapter.SetIpHelperStatus(pIfEntry->OperStatus);
				cAdapter.SetIPAddress(NULL);
				cAdapter.SetDhcpServer(NULL);
				cAdapter.SetGateway(NULL);
				cAdapter.SetIPNetMask(NULL);
				cAdapter.SetNetNumber(NULL);

				pList->AddTail(cAdapter);
				uIndex++;
			}
			else if (pIfEntry->OperStatus == IfOperStatusUp)
			{
				// Now parse the Adapter addresses
				for (pAdapterAddr = pAdresses; pAdapterAddr != NULL; pAdapterAddr = pAdapterAddr->Next)
				{
					if (pIfEntry->InterfaceIndex == pAdapterAddr->IfIndex)
					{
						if (pAdapterAddr->IfType != IF_TYPE_SOFTWARE_LOOPBACK)
						{
							// Get IP addresses
							for (pUnicast = pAdapterAddr->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next)
							{
								if (pUnicast)
								{
									family = pUnicast->Address.lpSockaddr->sa_family;
									// Get the Index
									cAdapter.SetIfIndex(pIfEntry->InterfaceIndex);
									// Get the type
									cAdapter.SetType(GetAdapterType(pIfEntry->Type));
									// Get the MIB type
									cAdapter.SetTypeMIB(GetIfType(pIfEntry->Type));
									// Get MAC Address 
									csMAC.Format(_T("%02X:%02X:%02X:%02X:%02X:%02X"),
										pIfEntry->PhysicalAddress[0], pIfEntry->PhysicalAddress[1],
										pIfEntry->PhysicalAddress[2], pIfEntry->PhysicalAddress[3],
										pIfEntry->PhysicalAddress[4], pIfEntry->PhysicalAddress[5]);
									cAdapter.SetMACAddress(csMAC);
									// Get the Speed
									cAdapter.SetSpeed(pIfEntry->TransmitLinkSpeed);
									// Get the status
									cAdapter.SetIpHelperStatus(pIfEntry->OperStatus);

									if (AF_INET == family)
									{
										csDescription = pIfEntry->Description;
										// Get the description
										cAdapter.SetDescription(csDescription);

										// IPV4
										char buf2[BUFSIZ];
										memset(buf2, 0, BUFSIZ);
										getnameinfo(pUnicast->Address.lpSockaddr, pUnicast->Address.iSockaddrLength, buf2, sizeof(buf2), NULL, 0, NI_NUMERICHOST);
										cAdapter.SetIPAddress(CA2W(buf2));

										//Get DHCP server address
										pDhcp = new SOCKET_ADDRESS;
										*pDhcp = pAdapterAddr->Dhcpv4Server;
										if (pDhcp)
										{
											char buf3[BUFSIZ];
											memset(buf3, 0, BUFSIZ);
											getnameinfo(pDhcp->lpSockaddr, pDhcp->iSockaddrLength, buf3, sizeof(buf3), NULL, 0, NI_NUMERICHOST);
											cAdapter.SetDhcpServer(CA2W(buf3));
											delete pDhcp;
										}

										// Now parse the Adapter addresses
										for (pAdapterAddrBis = pAdressesBis; pAdapterAddrBis != NULL; pAdapterAddrBis = pAdapterAddrBis->Next)
										{
											if (pIfEntry->InterfaceIndex == pAdapterAddrBis->IfIndex)
											{
												//Get gateway
												for (pGateway = pAdapterAddrBis->FirstGatewayAddress; pGateway != NULL; pGateway = pGateway->Next)
												{
													if (pGateway)
													{
														family = pGateway->Address.lpSockaddr->sa_family;
														if (AF_INET == family)
														{
															char buf4[BUFSIZ];
															memset(buf4, 0, BUFSIZ);
															getnameinfo(pGateway->Address.lpSockaddr, pGateway->Address.iSockaddrLength, buf4, sizeof(buf4), NULL, 0, NI_NUMERICHOST);
															cAdapter.SetGateway(CA2W(buf4));
															//Get subnet Mask
															// Make a second call to GetIpAddrTable to get the
															// actual data we want
															for (ifIndex = 0; ifIndex < (UINT)pIPAddrTable->dwNumEntries; ifIndex++)
															{
																if (pIfEntry->InterfaceIndex == pIPAddrTable->table[ifIndex].dwIndex)
																{
																	// Get NetMask
																	IPAddr.S_un.S_addr = (u_long)pIPAddrTable->table[ifIndex].dwMask;
																	IPAddrBis.S_un.S_addr = (u_long)pIPAddrTable->table[ifIndex].dwAddr;
																	csSubnet = inet_ntop(AF_INET, &IPAddr, str, INET_ADDRSTRLEN);
																	csAddressIp = inet_ntop(AF_INET, &IPAddrBis, bufferstr, INET_ADDRSTRLEN);

																	inet_pton(AF_INET, bufferstr, &ipAdr);
																	inet_pton(AF_INET, str, &ipMsk);
																	nbRez = htonl(ipAdr & ipMsk);

																	ipa.S_un.S_addr = htonl(nbRez);
																	csSubnetNetwork = inet_ntop(AF_INET, &ipa, bufferRez, INET_ADDRSTRLEN);
																	cAdapter.SetNetNumber(csSubnetNetwork);
																}
															}
															cAdapter.SetIPNetMask(csSubnet);
														}
													}
												}
											}
										}
										pAdapterAddrBis = NULL;
										pList->AddTail(cAdapter);
									}
									else if (AF_INET6 == family)
									{
										// IPv6
										SOCKADDR_IN6* ipv6 = reinterpret_cast<SOCKADDR_IN6*>(pUnicast->Address.lpSockaddr);
										char ipv6_buffer[INET6_ADDRSTRLEN] = { 0 };
										inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipv6_buffer, INET6_ADDRSTRLEN);
										csAddressIp.Format(_T("%s"), CA2W(ipv6_buffer));

										if (csAddressIp.Mid(0, 2) != _T("fe"))
										{
											cAdapter.SetIPAddress(CA2W(ipv6_buffer));

											csDescription.Format(_T("%s %s"), pIfEntry->Description, _T("(IPV6)"));
											// Get the description
											cAdapter.SetDescription(csDescription);

											// IPv6
											SOCKADDR_IN6* ipv6 = reinterpret_cast<SOCKADDR_IN6*>(pUnicast->Address.lpSockaddr);
											char ipv6_buffer[INET6_ADDRSTRLEN] = { 0 };
											inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipv6_buffer, INET6_ADDRSTRLEN);
											cAdapter.SetIPAddress(CA2W(ipv6_buffer));

											//Get prefix
											pPrefix = pAdapterAddr->FirstPrefix;

											if (pPrefix)
											{
												char buf_prefix[BUFSIZ];
												memset(buf_prefix, 0, BUFSIZ);
												getnameinfo(pPrefix->Address.lpSockaddr, pPrefix->Address.iSockaddrLength, buf_prefix, sizeof(buf_prefix), NULL, 0, NI_NUMERICHOST);
												cAdapter.SetNetNumber(CA2W(buf_prefix));

												csPrefixLength.Format(_T("%d"), pPrefix->PrefixLength);
												cAdapter.SetIPNetMask(csPrefixLength);
											}

											//Get DHCP server address
											pDhcp = new SOCKET_ADDRESS;
											*pDhcp = pAdapterAddr->Dhcpv6Server;
											if (pDhcp)
											{
												char buf3[BUFSIZ];
												memset(buf3, 0, BUFSIZ);
												getnameinfo(pDhcp->lpSockaddr, pDhcp->iSockaddrLength, buf3, sizeof(buf3), NULL, 0, NI_NUMERICHOST);
												cAdapter.SetDhcpServer(CA2W(buf3));
												delete pDhcp;
											}

											// Now parse the Adapter addresses
											for (pAdapterAddrBis = pAdressesBis; pAdapterAddrBis != NULL; pAdapterAddrBis = pAdapterAddrBis->Next)
											{
												if (pIfEntry->InterfaceIndex == pAdapterAddrBis->IfIndex)
												{
													//Get gateway
													for (pGateway = pAdapterAddrBis->FirstGatewayAddress; pGateway != NULL; pGateway = pGateway->Next)
													{
														if (pGateway)
														{
															family = pGateway->Address.lpSockaddr->sa_family;
															if (AF_INET6 == family)
															{
																SOCKADDR_IN6* gateway = reinterpret_cast<SOCKADDR_IN6*>(pGateway->Address.lpSockaddr);
																char gateway_buffer[INET6_ADDRSTRLEN] = { 0 };
																inet_ntop(AF_INET6, &(gateway->sin6_addr), gateway_buffer, INET6_ADDRSTRLEN);
																cAdapter.SetGateway(CA2W(gateway_buffer));
															}
														}
													}
												}
											}
											pAdapterAddrBis = NULL;
											pList->AddTail(cAdapter);
										}
									}
								}
							}
						}
					}
					uIndex++;
				}
			}
		}
	}
	
	
	// Free memory
	free(pIPAddrTable);
	FreeMibTable(pIfTable);
	free(pAdresses);
	free(pAdressesBis);
	pAdapterAddr = NULL;
	pAdapterAddrBis = NULL;
	// Unload library
	FreeLibrary(hDll);

	WSACleanup();
	

	AddLog(_T("OK\n"));
	if (uIndex > 0)
	{
		AddLog(_T("IpHlpAPI GetNetworkAdapters: OK (%u objects).\n"), uIndex);
		return TRUE;
	}
	AddLog(_T("IpHlpAPI GetNetworkAdapters: Failed because no network adapter object !\n"));
	return FALSE;
}


BOOL CIPHelper::GetNetworkAdaptersJustMAC(CNetworkAdapterList *pList)
{
	HINSTANCE			hDll;
	DWORD(WINAPI *lpfnGetIfTable)(PMIB_IFTABLE pIfTable, PULONG pdwSize, BOOL bOrder);
	PMIB_IFTABLE		pIfTable;
	PMIB_IFROW			pIfEntry;
	ULONG				ulLength = 0;
	UINT				uIndex = 0;
	DWORD				dwIndex;
	CNetworkAdapter		cAdapter;
	CString				csMAC,
		csAddress,
		csGateway,
		csDhcpServer,
		csBuffer;

	AddLog(_T("IpHlpAPI GetNetworkAdapters...\n"));
	// Reset network adapter list content
	while (!(pList->GetCount() == 0))
		pList->RemoveHead();
	// Load the IpHlpAPI dll and get the addresses of necessary functions
	if ((hDll = LoadLibrary(_T("iphlpapi.dll"))) < (HINSTANCE)HINSTANCE_ERROR)
	{
		// Cannot load IpHlpAPI MIB
		AddLog(_T("IpHlpAPI GetNetworkAdapters: Failed because unable to load <iphlpapi.dll> !\n"));
		hDll = NULL;
		return FALSE;
	}
	if ((*(FARPROC*)&lpfnGetIfTable = GetProcAddress(hDll, "GetIfTable")) == NULL)
	{
		// Tell the user that we could not find a usable IpHlpAPI DLL.                                  
		FreeLibrary(hDll);
		AddLog(_T("IpHlpAPI GetNetworkAdapters: Failed because unable to load <iphlpapi.dll> !\n"));
		return FALSE;
	}

	// Call GetIfTable to get memory size
	AddLog(_T("IpHlpAPI GetNetworkAdapters: Calling GetIfTable to determine network adapter properties..."));
	pIfTable = NULL;
	ulLength = 0;
	switch (lpfnGetIfTable(pIfTable, &ulLength, TRUE))
	{
	case NO_ERROR: // No error => no adapters
		FreeLibrary(hDll);
		AddLog(_T("Failed because no network adapters !\n"));
		return FALSE;
	case ERROR_NOT_SUPPORTED: // Not supported
		FreeLibrary(hDll);
		AddLog(_T("Failed because OS not support GetIfTable API function !\n"));
		return FALSE;
	case ERROR_BUFFER_OVERFLOW: // We must allocate memory
	case ERROR_INSUFFICIENT_BUFFER:
		break;
	default:
		FreeLibrary(hDll);
		AddLog(_T("Failed because unknown error !\n"));
		return FALSE;
	}
	if ((pIfTable = (PMIB_IFTABLE)malloc(ulLength + 1)) == NULL)
	{
		FreeLibrary(hDll);
		AddLog(_T("Failed because memory error !\n"));
		return FALSE;
	}
	// Recall GetIfTable
	switch (lpfnGetIfTable(pIfTable, &ulLength, TRUE))
	{
	case NO_ERROR: // No error
		break;
	case ERROR_NOT_SUPPORTED: // Not supported
		free(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because OS not support GetIfTable API function !\n"));
		return FALSE;
	case ERROR_BUFFER_OVERFLOW: // We have allocated needed memory, but not sufficient
	case ERROR_INSUFFICIENT_BUFFER:
		free(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because memory error !\n"));
		return FALSE;
	default:
		free(pIfTable);
		FreeLibrary(hDll);
		AddLog(_T("Failed because unknown error !\n"));
		return FALSE;
	}

	std::set<CString> lCompareStr;

	// Call GetIfEntry for each interface
	for (dwIndex = 0; dwIndex < pIfTable->dwNumEntries; dwIndex++)
	{
		pIfEntry = &(pIfTable->table[dwIndex]);

		if (pIfEntry->dwType != IF_TYPE_ETHERNET_CSMACD || pIfEntry->dwPhysAddrLen == 0)
			continue;

		// Get MAC Address 
		csMAC.Format(_T("%02X:%02X:%02X:%02X:%02X:%02X"),
			pIfEntry->bPhysAddr[0], pIfEntry->bPhysAddr[1],
			pIfEntry->bPhysAddr[2], pIfEntry->bPhysAddr[3],
			pIfEntry->bPhysAddr[4], pIfEntry->bPhysAddr[5]);

		if (lCompareStr.find(csMAC) != lCompareStr.end())
			continue;

		cAdapter.SetMACAddress(csMAC);

		lCompareStr.insert(csMAC);

		pList->AddTail(cAdapter);
		uIndex++;
	}
	// Free memory
	free(pIfTable);

	// Unload library
	FreeLibrary(hDll);

	AddLog(_T("OK\n"));
	if (uIndex > 0)
	{
		AddLog(_T("IpHlpAPI GetNetworkAdapters: OK (%u objects).\n"), uIndex);
		return TRUE;
	}
	AddLog(_T("IpHlpAPI GetNetworkAdapters: Failed because no network adapter object !\n"));
	return FALSE;
}