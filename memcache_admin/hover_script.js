// $Id$

Drupal.memcacheHoverData = function() {
  $('div.memcache_data').each(function() {
    var fadeArea = $(this).find('pre'), staticOffsetY = null, staticOffsetX = null;
    
    
    $(this).find('p').mouseover(hoverOver);
    $(this).find('p').mousemove(hoverMove);
    $(this).find('p').mouseout(hoverOut);


    function hoverOver(e) {
      staticOffsetX = -$(this)[0].offsetWidth;
      staticOffsetY = -fadeArea[0].offsetHeight - $(this)[0].offsetHeight;
      fadeArea.css('opacity', 1.00);
    };
    
    
    function hoverMove(e) {
      fadeArea.css('left', Math.round(Drupal.mousePosition(e).x + staticOffsetX) + "px");
      fadeArea.css('top',  Math.round(Drupal.mousePosition(e).y + staticOffsetY) + "px");
    };
    

    function hoverOut(e) {
      fadeArea.css('opacity', 0.00);
    };
  });
};



if (Drupal.jsEnabled) {
  $(document).ready(Drupal.memcacheHoverData);
}
