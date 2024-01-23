/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
 * Copyright notice:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#pragma once


// defines PID
typedef enum {
	PAT_PID=0x0000,
	CAT_PID=0x0001,
	TSDT_PID=0x0002,
	NIT_PID=0x0010,
	ST_PID= 0x0010,
	SDT_PID=0x0011,
	EIT_PID=0x0012,
	TDT_PID=0x0014,
	MHW_PID1=0x00D2,
	MHW_PID2=0x00D3,
	FREESAT_EIT_PID=3842,
	FREESAT_EIT_PID2=3843,
	FREESAT_INFO_EIT_PID=3003,
	FREESAT_INFO_EIT_PF_PID=3004,
	PREMIERE_EPG_PID1=0x0b11,
	PREMIERE_EPG_PID2=0x0b12,
	EEPG_PID=0x0300,
} pid_type_t;

#define FREESAT_TS_ID (2315)

#ifndef null_pid
#define null_pid 8191
#endif

#define SECTION_SIZE (1024) //max. size of a section
#define EIT_SECTION_SIZE (4096) //max. size of an EIT section
#define PAT_SIZE (SECTION_SIZE) //maximum size of a PAT section
#define TP_NUM_HANDLES (32)


//#include "pmt.h"


typedef enum {
	TABLE_ID_PAT=                    0x00,
	TABLE_ID_PMT=                    0x02,
	TABLE_ID_TDT=                    0x70,
	TABLE_ID_TOT=                    0x73,
	TABLE_ID_NIT_ACTUAL=             0x40,
	TABLE_ID_NIT_OTHER=              0x41,
	TABLE_ID_SDT_ACTUAL=             0x42,
	TABLE_ID_SDT_OTHER=              0x46,
	TABLE_IDS_SDT=                   ((TABLE_ID_SDT_ACTUAL<<8)|TABLE_ID_SDT_OTHER),
	TABLE_ID_BAT=                    0x4A,
	TABLE_ID_EIT_PF_ACTUAL=          0x4E,
	TABLE_ID_EIT_PF_OTHER=           0x4F,
	TABLE_ID_EIT_SCH_ACTUAL_LOW=     0x50,
	TABLE_ID_EIT_SCH_ACTUAL_HIGH=    0x5F,
	TABLE_ID_EIT_SCH_OTHER_LOW=      0x60,
	TABLE_ID_EIT_SCH_OTHER_HIGH=     0x6F,
	TABLE_ID_EEPG_LOW=				0x81,
	TABLE_ID_EEPG_HIGH=				0xa4,
	TABLE_IDS_EIT_SCH_ACTUAL=       256+TABLE_ID_EIT_SCH_ACTUAL_LOW,
	TABLE_IDS_EIT_SCH_OTHER=        256+TABLE_ID_EIT_SCH_OTHER_LOW,
	TABLE_IDS_PREM_FEEDINFO=        512+TABLE_ID_EIT_PF_ACTUAL,
	TABLE_IDS_FREESAT=              768,
	TABLE_ID_EEPG=0x1000
} si_table_id_t;

typedef enum {
	PID_PAT=                   0x0000,
	PID_CAT=                   0x0001,
	PID_TSDT=                  0x0002, //transport streams desccription table
	PID_NIT=                   0x0010,
	PID_SDT_BAT=               0x0011,
	PID_EIT=                   0x0012,
	PID_RST=                   0x0013, //running status
	PID_TDT_TOT=               0x0014, //Time and tadte + time offset
	PID_DIT=                   0x001e, //discontinuity information table for partial transport streams
	PID_SIT=                   0x001f, //selection information table for partial transport streams
	PID_SKY_TITLE_LOW=         0x0030,
	PID_SKY_TITLE_HIGH=        0x0037,
	PID_SKY_SUMMARY_LOW=       0x0040,
	PID_SKY_SUMMARY_HIGH=      0x0047
#if 0
	PID_EEPG=				    ,
	PIDS_PREM_FEEDINFO=     ,
	PIDS_FREESAT=           ,
	PID_EEPG=0x1000
#endif
} si_pid_t;

namespace SI {

enum DescriptorTag {
  // defined by ISO/IEC 13818-1
	             Descriptor25Tag = 0x19,
	             Descriptor26Tag = 0x1a,
	             Descriptor46Tag = 0x2e,
	             Descriptor143Tag = 0x8f,
	             Descriptor240Tag = 0xf0,
               VideoStreamDescriptorTag = 0x02,
               AudioStreamDescriptorTag = 0x03,
               HierarchyDescriptorTag = 0x04,
               RegistrationDescriptorTag = 0x05,
               DataStreamAlignmentDescriptorTag = 0x06,
               TargetBackgroundGridDescriptorTag = 0x07,
               VideoWindowDescriptorTag = 0x08,
               CaDescriptorTag = 0x09,
               ISO639LanguageDescriptorTag = 0x0A,
               SystemClockDescriptorTag = 0x0B,
               MultiplexBufferUtilizationDescriptorTag = 0x0C,
               CopyrightDescriptorTag = 0x0D,
               MaximumBitrateDescriptorTag = 0x0E,
               PrivateDataIndicatorDescriptorTag = 0x0F,
               SmoothingBufferDescriptorTag = 0x10,
               STDDescriptorTag = 0x11,
               IBPDescriptorTag = 0x12,
  // defined by ISO-13818-6 (DSM-CC)
               CarouselIdentifierDescriptorTag = 0x13,
               // 0x14 - 0x3F  Reserved
  // defined by ISO/IEC 13818-1 Amendment
               AVCDescriptorTag = 0x28,
               SVCExtensionDescriptorTag = 0x30,
               MVCExtensionDescriptorTag = 0x31,
  // defined by ETSI (EN 300 468)
               NetworkNameDescriptorTag = 0x40,
               ServiceListDescriptorTag = 0x41,
               StuffingDescriptorTag = 0x42,
               SatelliteDeliverySystemDescriptorTag = 0x43,
               CableDeliverySystemDescriptorTag = 0x44,
               VBIDataDescriptorTag = 0x45,
               VBITeletextDescriptorTag = 0x46,
               BouquetNameDescriptorTag = 0x47,
               ServiceDescriptorTag = 0x48,
               CountryAvailabilityDescriptorTag = 0x49,
               LinkageDescriptorTag = 0x4A,
               NVODReferenceDescriptorTag = 0x4B,
               TimeShiftedServiceDescriptorTag = 0x4C,
               ShortEventDescriptorTag = 0x4D,
               ExtendedEventDescriptorTag = 0x4E,
               TimeShiftedEventDescriptorTag = 0x4F,
               ComponentDescriptorTag = 0x50,
               MocaicDescriptorTag = 0x51,
               StreamIdentifierDescriptorTag = 0x52,
               CaIdentifierDescriptorTag = 0x53,
               ContentDescriptorTag = 0x54,
               ParentalRatingDescriptorTag = 0x55,
               TeletextDescriptorTag = 0x56,
               TelephoneDescriptorTag = 0x57,
               LocalTimeOffsetDescriptorTag = 0x58,
               SubtitlingDescriptorTag = 0x59,
               TerrestrialDeliverySystemDescriptorTag = 0x5A,
               MultilingualNetworkNameDescriptorTag = 0x5B,
               MultilingualBouquetNameDescriptorTag = 0x5C,
               MultilingualServiceNameDescriptorTag = 0x5D,
               MultilingualComponentDescriptorTag = 0x5E,
               PrivateDataSpecifierDescriptorTag = 0x5F,
               ServiceMoveDescriptorTag = 0x60,
               ShortSmoothingBufferDescriptorTag = 0x61,
               FrequencyListDescriptorTag = 0x62,
               PartialTransportStreamDescriptorTag = 0x63,
               DataBroadcastDescriptorTag = 0x64,
               ScramblingDescriptorTag = 0x65,
               DataBroadcastIdDescriptorTag = 0x66,
               TransportStreamDescriptorTag = 0x67,
               DSNGDescriptorTag = 0x68,
               PDCDescriptorTag = 0x69,
               AC3DescriptorTag = 0x6A,
               AncillaryDataDescriptorTag = 0x6B,
               CellListDescriptorTag = 0x6C,
               CellFrequencyLinkDescriptorTag = 0x6D,
               AnnouncementSupportDescriptorTag = 0x6E,
               ApplicationSignallingDescriptorTag = 0x6F,
               AdaptationFieldDataDescriptorTag = 0x70,
               ServiceIdentifierDescriptorTag = 0x71,
               ServiceAvailabilityDescriptorTag = 0x72,
  // defined by ETSI (EN 300 468) v 1.7.1
               DefaultAuthorityDescriptorTag = 0x73,
               RelatedContentDescriptorTag = 0x74,
               TVAIdDescriptorTag = 0x75,
               ContentIdentifierDescriptorTag = 0x76,
               TimeSliceFecIdentifierDescriptorTag = 0x77,
               ECMRepetitionRateDescriptorTag = 0x78,
               S2SatelliteDeliverySystemDescriptorTag = 0x79,
               EnhancedAC3DescriptorTag = 0x7A,
               DTSDescriptorTag = 0x7B,
               AACDescriptorTag = 0x7C,
							 Reserved7eTag = 0x7e,
               ExtensionDescriptorTag = 0x7F,
 // defined by EICTA/EACEM/DIGITALEUROPE
							 UserDefined81Tag = 0x81,
							 UserDefined82Tag = 0x82,
               LogicalChannelDescriptorTag = 0x83,
               PreferredNameListDescriptorTag = 0x84,
               PreferredNameIdentifierDescriptorTag = 0x85,
               EacemStreamIdentifierDescriptorTag = 0x86,
               HdSimulcastLogicalChannelDescriptorTag = 0x88,
 // Extension descriptors
               ImageIconDescriptorTag = 0x00,
               CpcmDeliverySignallingDescriptor = 0x01,
               CPDescriptorTag = 0x02,
               CPIdentifierDescriptorTag = 0x03,
               T2DeliverySystemDescriptorTag = 0x04,
               SHDeliverySystemDescriptorTag = 0x05,
               SupplementaryAudioDescriptorTag = 0x06,
               NetworkChangeNotifyDescriptorTag = 0x07,
               MessageDescriptorTag = 0x08,
               TargetRegionDescriptorTag = 0x09,
               TargetRegionNameDescriptorTag = 0x0A,
               ServiceRelocatedDescriptorTag = 0x0B,
 // defined by ETSI (EN 300 468) v 1.12.1
               XAITPidDescriptorTag = 0x0C,
               C2DeliverySystemDescriptorTag = 0x0D,
               // 0x0E - 0x0F Reserved
               VideoDepthRangeDescriptorTag = 0x10,
               T2MIDescriptorTag = 0x11,

 // Defined by ETSI TS 102 812 (MHP)
               // They once again start with 0x00 (see page 234, MHP specification)
							 OtvServiceListDescriptorTag = 0xb1,
							 ReturnTransmissionModeTag = 0xb2,
               MHP_ApplicationDescriptorTag = 0x00,
               MHP_ApplicationNameDescriptorTag = 0x01,
               MHP_TransportProtocolDescriptorTag = 0x02,
               MHP_DVBJApplicationDescriptorTag = 0x03,
               MHP_DVBJApplicationLocationDescriptorTag = 0x04,
               // 0x05 - 0x0A is unimplemented this library
               MHP_ExternalApplicationAuthorisationDescriptorTag = 0x05,
               MHP_IPv4RoutingDescriptorTag = 0x06,
               MHP_IPv6RoutingDescriptorTag = 0x07,
               MHP_DVBHTMLApplicationDescriptorTag = 0x08,
               MHP_DVBHTMLApplicationLocationDescriptorTag = 0x09,
               MHP_DVBHTMLApplicationBoundaryDescriptorTag = 0x0A,
               MHP_ApplicationIconsDescriptorTag = 0x0B,
               MHP_PrefetchDescriptorTag = 0x0C,
               MHP_DelegatedApplicationDescriptorTag = 0x0E,
               MHP_ApplicationStorageDescriptorTag = 0x10,
               MHP_SimpleApplicationLocationDescriptorTag = 0x15,
               MHP_SimpleApplicationBoundaryDescriptorTag = 0x17,
  // Premiere private Descriptor Tags
               PremiereContentTransmissionDescriptorTag = 0xF2,
               //a descriptor currently unimplemented in this library
               //the actual value 0xFF is "forbidden" according to the spec.
               ForbiddenDescriptorTag = 0xFF,
							 //freesat
							 FSTServiceListDescriptorTag = 0xD3,
							 FSTRegionListDescriptorTag = 0xD4,
							 FSTChannelCategoryDescriptorTag = 0xD5,
							 FSTCategoryDescriptorTag = 0xD8,
};

enum DescriptorTagDomain { SI, MHP, PCIT };

enum RunningStatus { RunningStatusUndefined = 0,
                     RunningStatusNotRunning = 1,
                     RunningStatusStartsInAFewSeconds = 2,
                     RunningStatusPausing = 3,
                     RunningStatusRunning = 4
                   };

enum LinkageType { LinkageTypeInformationService = 0x01,
                   LinkageTypeEPGService = 0x02,
                   LinkageTypeCaReplacementService = 0x03,
                   LinkageTypeTSContainingCompleteNetworkBouquetSi = 0x04,
                   LinkageTypeServiceReplacementService = 0x05,
                   LinkageTypeDataBroadcastService = 0x06,
                   LinkageTypeRCSMap = 0x07,
                   LinkageTypeMobileHandover = 0x08,
                   LinkageTypeSystemSoftwareUpdateService = 0x09,
                   LinkageTypeTSContainingSsuBatOrNit = 0x0A,
                   LinkageTypePremiere = 0xB0
                 };

enum AudioType { AudioTypeUndefined = 0x00,
                 AudioTypeCleanEffects = 0x01,
                 AudioTypeHearingImpaired = 0x02,
                 AudioTypeVisualImpairedCommentary = 0x03
               };
}; //end namespace SI
