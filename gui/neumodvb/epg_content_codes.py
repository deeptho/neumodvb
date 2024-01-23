#!/usr/bin/python3
# Neumo dvb (C) 2019-2024 deeptho@gmail.com
# Copyright notice:
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

content_dict = {
    0: [
    ],
    1: [
        ["Movie", "Drama"],
        ["Detective", "Thriller"],
        ["Adventure", "Western", "War"],
        ["Science fiction", "Fantasy", "Horror"],
        ["Comedy"],
        ["Soap", "Melodrama", "Folkloric"],
        ["Romance"],
        ["Serious", "Classical", "Religious", "Historical movie", "Drama"],
        ["Adult movie", "Drama"],
        ["Movie", "drama"],
        ["Movie", "drama"],
        ["Movie", "drama"],
        ["Movie", "drama"],
        ["Movie", "drama"],
        ["Movie", "drama"],
        ["Movie", "drama"]
    ],
    2: [
        ["News", "Current affairs"],
        ["News", "Weather report"],
        ["News magazine"],
        ["Documentary"],
        ["Discussion", "Interview", "Debate"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"],
        ["News", "Current Affairs"]
    ],
    3: [
        ["Show", "Game show"],
        ["Game show", "Quiz", "Contest"],
        ["Variety show"],
        ["Talk show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
        ["Show", "Game show"],
    ],
    4: [
        ["Sports"],
        ["Special events", "(Olympic Games, World Cup, etc.)"],
        ["Sports magazines"],
        ["Football", "Soccer"],
        ["Tennis", "Squash"],
        ["Team sports", "(excluding football)"],
        ["Athletics"],
        ["Motor sport"],
        ["Water sport"],
        ["Winter sports"],
        ["Equestrian"],
        ["Martial sports"],
        ["Sports"],
        ["Sports"],
        ["Sports"],
        ["Sports"],
    ],
    5: [
        ["Children's", "Youth programs"],
        ["Pre-school children's programs"],
        ["Entertainment programs for 6 to 14"],
        ["Entertainment programs for 10 to 16"],
        ["Informational", "Educational", "School programs"],
        ["Cartoons", "Puppets"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
        ["Children's", "Youth Programs"],
    ],
    6: [
        ["Music", "Ballet", "Dance"],
        ["Rock", "Pop"],
        ["Serious music", "Classical music"],
        ["Folk", "Traditional music"],
        ["Jazz"],
        ["Musical", "Opera"],
        ["Ballet"],
        ["Music", "Ballet", "Dance"],
        ["Music", "Ballet", "Dance"],
        ["Music", "Ballet", "Dance"],
        ["Music", "Ballet", "Dance"],
        ["Music", "Ballet", "Dance"],
        ["Music", "Ballet", "Dance"],
        ["Music", "Ballet", "Dance"],
        ["Music", "Ballet", "Dance"],
        ["Music", "Ballet", "Dance"],
    ],
    7: [
        ["Arts", "Culture", "(without music)"],
        ["Performing arts"],
        ["Fine arts"],
        ["Religion"],
        ["Popular culture", "Traditional arts"],
        ["Literature"],
        ["Film", "Cinema"],
        ["Experimental film", "Video"],
        ["Broadcasting", "Press"],
        ["New media"],
        ["Arts magazines", "Culture magazines"],
        ["Fashion"],
        ["Arts", "Culture" "(without music)"],
        ["Arts", "Culture", "(without music)"],
        ["Arts", "Culture", "(without music)"],
        ["Arts", "Culture", "(without music)"],
    ],
    8: [
        ["Social", "Political issues", "Economics"],
        ["Magazines", "Reports", "Documentary"],
        ["Economics", "Social advisory"],
        ["Remarkable people"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
        ["Social", "Political issues", "Economics"],
    ],
    9: [
        ["Education", "Science", "Factual topics"],
        ["Nature", "Animals", "Environment"],
        ["Technology", "Natural sciences"],
        ["Medicine", "Physiology", "Psychology"],
        ["Foreign countries", "Expeditions"],
        ["Social", "Spiritual sciences"],
        ["Further education"],
        ["Languages"],
        ["Education", "Science", "Factual topics"],
        ["Education", "Science", "Factual topics"],
        ["Education", "Science", "Factual topics"],
        ["Education", "Science", "Factual topics"],
        ["Education", "Science", "Factual topics"],
        ["Education", "Science", "Factual topics"],
        ["Education", "Science", "Factual topics"],
        ["Education", "Science", "Factual topics"],
    ],
    10: [
        ["Leisure", "hobbies"],
        ["Tourism", "Travel"],
        ["Handicraft"],
        ["Motoring"],
        ["Fitness and health"],
        ["Cooking"],
        ["Advertisement", "Shopping"],
        ["Gardening"],
        ["Leisure", "hobbies"],
        ["Leisure", "hobbies"],
        ["Leisure", "hobbies"],
        ["Leisure", "hobbies"],
        ["Leisure", "hobbies"],
        ["Leisure", "hobbies"],
        ["Leisure", "hobbies"],
        ["Leisure", "hobbies"]
    ]
}

dd={}
for k,v in content_dict.items():
    for k1,v1 in enumerate(v):
        dd[k*16+k1] =v1[0]


def content_type_name(content_code):
    return dd.get(content_code, f'{hex(content_code)}')
