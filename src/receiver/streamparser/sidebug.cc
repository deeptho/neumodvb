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

#include "sidebug.h"
#include "util/dtassert.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static char* descriptor_names[256] = {NULL};
static bool descriptor_names_loaded = false;

static void load_descriptor_name(const char* name, int tagno) {
	assert(tagno <= 256);
	descriptor_names[tagno] = (char*)name;
}

static void load_descriptor_names() {
	memset(descriptor_names, 0, sizeof(descriptor_names));
	// defined by ISO/IEC 13818-1nfo
	load_descriptor_name("VideoStreamDescriptorTag", 0x02);
	load_descriptor_name("AudioStreamDescriptorTag", 0x03);
	load_descriptor_name("HierarchyDescriptorTag", 0x04);
	load_descriptor_name("RegistrationDescriptorTag", 0x05);
	load_descriptor_name("DataStreamAlignmentDescriptorTag", 0x06);
	load_descriptor_name("TargetBackgroundGridDescriptorTag", 0x07);
	load_descriptor_name("VideoWindowDescriptorTag", 0x08);
	load_descriptor_name("CaDescriptorTag", 0x09);
	load_descriptor_name("ISO639LanguageDescriptorTag", 0x0A);
	load_descriptor_name("SystemClockDescriptorTag", 0x0B);
	load_descriptor_name("MultiplexBufferUtilizationDescriptorTag", 0x0C);
	load_descriptor_name("CopyrightDescriptorTag", 0x0D);
	load_descriptor_name("MaximumBitrateDescriptorTag", 0x0E);
	load_descriptor_name("PrivateDataIndicatorDescriptorTag", 0x0F);
	load_descriptor_name("SmoothingBufferDescriptorTag", 0x10);
	load_descriptor_name("STDDescriptorTag", 0x11);
	load_descriptor_name("IBPDescriptorTag", 0x12);
	// defined by ISO-13818-6 (DSM-CC)
	load_descriptor_name("CarouselIdentifierDescriptorTag", 0x13);
	// 0x14 - 0x3F  Reserved
	// defined by ISO/IEC 13818-1 Amendment
	load_descriptor_name("AVCDescriptorTag", 0x28);
	load_descriptor_name("SVCExtensionDescriptorTag", 0x30);
	load_descriptor_name("MVCExtensionDescriptorTag", 0x31);
	// defined by ETSI (EN 300 468)
	load_descriptor_name("NetworkNameDescriptorTag", 0x40);
	load_descriptor_name("ServiceListDescriptorTag", 0x41);
	load_descriptor_name("StuffingDescriptorTag", 0x42);
	load_descriptor_name("SatelliteDeliverySystemDescriptorTag", 0x43);
	load_descriptor_name("CableDeliverySystemDescriptorTag", 0x44);
	load_descriptor_name("VBIDataDescriptorTag", 0x45);
	load_descriptor_name("VBITeletextDescriptorTag", 0x46);
	load_descriptor_name("BouquetNameDescriptorTag", 0x47);
	load_descriptor_name("ServiceDescriptorTag", 0x48);
	load_descriptor_name("CountryAvailabilityDescriptorTag", 0x49);
	load_descriptor_name("LinkageDescriptorTag", 0x4A);
	load_descriptor_name("NVODReferenceDescriptorTag", 0x4B);
	load_descriptor_name("TimeShiftedServiceDescriptorTag", 0x4C);
	load_descriptor_name("ShortEventDescriptorTag", 0x4D);
	load_descriptor_name("ExtendedEventDescriptorTag", 0x4E);
	load_descriptor_name("TimeShiftedEventDescriptorTag", 0x4F);
	load_descriptor_name("ComponentDescriptorTag", 0x50);
	load_descriptor_name("MocaicDescriptorTag", 0x51);
	load_descriptor_name("StreamIdentifierDescriptorTag", 0x52);
	load_descriptor_name("CaIdentifierDescriptorTag", 0x53);
	load_descriptor_name("ContentDescriptorTag", 0x54);
	load_descriptor_name("ParentalRatingDescriptorTag", 0x55);
	load_descriptor_name("TeletextDescriptorTag", 0x56);
	load_descriptor_name("TelephoneDescriptorTag", 0x57);
	load_descriptor_name("LocalTimeOffsetDescriptorTag", 0x58);
	load_descriptor_name("SubtitlingDescriptorTag", 0x59);
	load_descriptor_name("TerrestrialDeliverySystemDescriptorTag", 0x5A);
	load_descriptor_name("MultilingualNetworkNameDescriptorTag", 0x5B);
	load_descriptor_name("MultilingualBouquetNameDescriptorTag", 0x5C);
	load_descriptor_name("MultilingualServiceNameDescriptorTag", 0x5D);
	load_descriptor_name("MultilingualComponentDescriptorTag", 0x5E);
	load_descriptor_name("PrivateDataSpecifierDescriptorTag", 0x5F);
	load_descriptor_name("ServiceMoveDescriptorTag", 0x60);
	load_descriptor_name("ShortSmoothingBufferDescriptorTag", 0x61);
	load_descriptor_name("FrequencyListDescriptorTag", 0x62);
	load_descriptor_name("PartialTransportStreamDescriptorTag", 0x63);
	load_descriptor_name("DataBroadcastDescriptorTag", 0x64);
	load_descriptor_name("ScramblingDescriptorTag", 0x65);
	load_descriptor_name("DataBroadcastIdDescriptorTag", 0x66);
	load_descriptor_name("TransportStreamDescriptorTag", 0x67);
	load_descriptor_name("DSNGDescriptorTag", 0x68);
	load_descriptor_name("PDCDescriptorTag", 0x69);
	load_descriptor_name("AC3DescriptorTag", 0x6A);
	load_descriptor_name("AncillaryDataDescriptorTag", 0x6B);
	load_descriptor_name("CellListDescriptorTag", 0x6C);
	load_descriptor_name("CellFrequencyLinkDescriptorTag", 0x6D);
	load_descriptor_name("AnnouncementSupportDescriptorTag", 0x6E);
	load_descriptor_name("ApplicationSignallingDescriptorTag", 0x6F);
	load_descriptor_name("AdaptationFieldDataDescriptorTag", 0x70);
	load_descriptor_name("ServiceIdentifierDescriptorTag", 0x71);
	load_descriptor_name("ServiceAvailabilityDescriptorTag", 0x72);
	// defined by ETSI (EN 300 468) v 1.7.1
	load_descriptor_name("DefaultAuthorityDescriptorTag", 0x73);
	load_descriptor_name("RelatedContentDescriptorTag", 0x74);
	load_descriptor_name("TVAIdDescriptorTag", 0x75);
	load_descriptor_name("ContentIdentifierDescriptorTag", 0x76);
	load_descriptor_name("TimeSliceFecIdentifierDescriptorTag", 0x77);
	load_descriptor_name("ECMRepetitionRateDescriptorTag", 0x78);
	load_descriptor_name("S2SatelliteDeliverySystemDescriptorTag", 0x79);
	load_descriptor_name("EnhancedAC3DescriptorTag", 0x7A);
	load_descriptor_name("DTSDescriptorTag", 0x7B);
	load_descriptor_name("AACDescriptorTag", 0x7C);
	load_descriptor_name("ExtensionDescriptorTag", 0x7F);
	// defined by EICTA/EACEM/DIGITALEUROPE
	load_descriptor_name("LogicalChannelDescriptorTag", 0x83);
	load_descriptor_name("PreferredNameListDescriptorTag", 0x84);
	load_descriptor_name("PreferredNameIdentifierDescriptorTag", 0x85);
	load_descriptor_name("EacemStreamIdentifierDescriptorTag", 0x86);
	load_descriptor_name("HdSimulcastLogicalChannelDescriptorTag", 0x88);
	// Extension descriptors
	load_descriptor_name("ImageIconDescriptorTag", 0x00);
	load_descriptor_name("CpcmDeliverySignallingDescriptor", 0x01);
	load_descriptor_name("CPDescriptorTag", 0x02);
	load_descriptor_name("CPIdentifierDescriptorTag", 0x03);
	load_descriptor_name("T2DeliverySystemDescriptorTag", 0x04);
	load_descriptor_name("SHDeliverySystemDescriptorTag", 0x05);
	load_descriptor_name("SupplementaryAudioDescriptorTag", 0x06);
	load_descriptor_name("NetworkChangeNotifyDescriptorTag", 0x07);
	load_descriptor_name("MessageDescriptorTag", 0x08);
	load_descriptor_name("TargetRegionDescriptorTag", 0x09);
	load_descriptor_name("TargetRegionNameDescriptorTag", 0x0A);
	load_descriptor_name("ServiceRelocatedDescriptorTag", 0x0B);
	// defined by ETSI (EN 300 468) v 1.12.1
	load_descriptor_name("XAITPidDescriptorTag", 0x0C);
	load_descriptor_name("C2DeliverySystemDescriptorTag", 0x0D);
	// 0x0E - 0x0F Reserved
	load_descriptor_name("VideoDepthRangeDescriptorTag", 0x10);
	load_descriptor_name("T2MIDescriptorTag", 0x11);

	// Defined by ETSI TS 102 812 (MHP)
	// They once again start with 0x00 (see page 234, MHP specification)
	load_descriptor_name("MHP_ApplicationDescriptorTag", 0x00);
	load_descriptor_name("MHP_ApplicationNameDescriptorTag", 0x01);
	load_descriptor_name("MHP_TransportProtocolDescriptorTag", 0x02);
	load_descriptor_name("MHP_DVBJApplicationDescriptorTag", 0x03);
	load_descriptor_name("MHP_DVBJApplicationLocationDescriptorTag", 0x04);
	// 0x05 - 0x0A is unimplemented this library
	load_descriptor_name("MHP_ExternalApplicationAuthorisationDescriptorTag", 0x05);
	load_descriptor_name("MHP_IPv4RoutingDescriptorTag", 0x06);
	load_descriptor_name("MHP_IPv6RoutingDescriptorTag", 0x07);
	load_descriptor_name("MHP_DVBHTMLApplicationDescriptorTag", 0x08);
	load_descriptor_name("MHP_DVBHTMLApplicationLocationDescriptorTag", 0x09);
	load_descriptor_name("MHP_DVBHTMLApplicationBoundaryDescriptorTag", 0x0A);
	load_descriptor_name("MHP_ApplicationIconsDescriptorTag", 0x0B);
	load_descriptor_name("MHP_PrefetchDescriptorTag", 0x0C);
	load_descriptor_name("MHP_DelegatedApplicationDescriptorTag", 0x0E);
	load_descriptor_name("MHP_ApplicationStorageDescriptorTag", 0x10);
	load_descriptor_name("MHP_SimpleApplicationLocationDescriptorTag", 0x15);
	load_descriptor_name("MHP_SimpleApplicationBoundaryDescriptorTag", 0x17);
	// Premiere private Descriptor Tags
	load_descriptor_name("PremiereContentTransmissionDescriptorTag", 0xF2);

	// a descriptor currently unimplemented in this library
	// the actual value 0xFF is "forbidden" according to the spec.
	load_descriptor_name("UnimplementedDescriptorTag", 0xFF);
};

const char* name_of_descriptor_tag(int tag) {
	if (!descriptor_names_loaded)
		load_descriptor_names();
	assert(tag < 256);
	if (!descriptor_names[tag])
		asprintf(&descriptor_names[tag], "<descriptor 0x%x>", tag);

	return descriptor_names[tag];
}
