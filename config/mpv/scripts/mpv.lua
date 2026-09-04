function set_specific_ratio()
  local originalAspect = mp.get_property_number("video-params/aspect")
  -- A very crude way to find 4:3 ratio. it varies as a float from 1.30 and up.
   if originalAspect >= 1.30 and originalAspect <= 1.39 then
      mp.set_property('video-aspect-override', '16:9')
   end
   -- A very crude way to detect wrong aspect ratio on hevc channel at 30W
  if originalAspect >= 1.8 then
      mp.set_property('video-aspect-override', '16:9')
   end
end

#mp.register_event("video-reconfig", set_specific_ratio)
