To install, move memcache.inc to your DRUPAL/includes directory (where the other *.inc files live).

Then patch bootstrap.inc using the bootstrap.patch file.

In your settings.php file you need to map out your memcache servers. Here is an example:

Example 1.
 $conf = array(
   'memcache_servers' => array('default' => array('localhost' => array(11211)),
    ),
 );

Example 1 will map a server at localhost:11211 which will be used as the default. It is good to
always specify a default and you may consider making it the biggest memory allocation.

Example 1.
 $conf = array(
   'memcache_servers' => array('default' => array('12.34.56.78' => array(11211, 11212, 11213)),
                               'cache'   => array('98.76.54.32' => array(33433)),
    ),
 );

Example 2 maps three allocations for 'default' at 12.34.56.78:11211, 12.34.56.78:11212, and
12.34.56.78:11213. These should act as a pool, meaning Memcache's failover should utilize all
three for the 'default' bin.

WARNING: Local tests show that failover is not working yet!

Then, a server is allocated to the 'cache' bin (98.76.54.32:33433). This means that anything get or set with the bin 'cache' will go there instead of to the other (default).