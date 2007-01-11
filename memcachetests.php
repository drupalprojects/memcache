<?php
// $ID$

/*
 * Some unit tests used while developing. You may find them instructional.
 * Place the memcachetests.php file in the root Drupal directory. Start memcache servers
 * on localhost:11211 and localhost:11212
 */

include_once './includes/memcache.inc';

print "<h1>Memcache configuration details</h1>";
print "<ul><li>memcache.allow_failover=". ini_get('memcache.allow_failover'). "</li>";
print "<li>memcache.max_failover_attempts=". ini_get('memcache.max_failover_attempts'). "</li>";
print "<li>memcache.chunk_size=". ini_get('memcache.chunk_size'). "</li>";
print "<li>memcache.default_port=". ini_get('memcache.default_port'). "</li>";
print "</ul>";

global $conf;

$conf = array(
 'memcache_servers' => array('default' => array('localhost' => array(11211)),
  ),
);


##############
// Begin Tests
##############

// Test 1. Connect to server and retrieve stats
###############################################
printHeader();
$mc = dmemcache_object();
formatStats($mc->getStats());

// Test 2. Set a number of keys and retrieve their values
#########################################################
printHeader();
$keys = array('a', time(), 'http://www.robshouse.net/home/page?q=xyz&p=x', 'What about space?');

print '<ol>';
foreach ($keys as $key) {
  testKeyValue($mc, $key, 'Hi Robert');
}
print '</ol>';

// Test 3. Set a number of PROBLEMATIC keys and retrieve their values
#####################################################################
printHeader();

print '<ol>';
$key = '   ';
testKeyValue($mc, $key, 'Hi Dude');

$key = "\n";
testKeyValue($mc, $key, 'Hi Dude 2');

print '<li><em>space and line break different?</em>='. $mc->get(' ').'</li>';
print '</ol>';

// Test 4. Test flushing the $mc object from dmemcache_object
#############################################################
printHeader();
formatStats($mc->getStats());
$conf = array(
 'memcache_servers' => array('default' => array('localhost' => array(11212)),
  ),
);
$mc = dmemcache_object('default', TRUE);
formatStats($mc->getStats());



// Test 5. Confirm that space and line break are treated as the same character
##############################################################################
printHeader();
$mc->set("\n", "This is a new line", FALSE, 5);
print '<ol>';
print '<li>'. $mc->get("\n"). '</li>';
print '<li>'. $mc->get(" "). '</li>';
print '</ol>';

// Test 6. Try out dmemcache_set and dmemcache_get
##################################################
printHeader();
$keys = array('a', time(), 'http://www.robshouse.net/home/page?q=xyz&p=x', 'What about space?');

print '<ol>';
foreach ($keys as $key) {
  dtestKeyValue($key, 'Hi Robert 2');
}
print '</ol>';

// Test 7. See if newline and space are identical using dmemcache
// conclusion: the urlencode() fixes this problem.
#################################################################
printHeader();
dmemcache_set("\n", "This is a new line");
print '<ol>';
print '<li>'. dmemcache_get("\n"). '</li>';
print '<li>'. dmemcache_get(" "). '</li>';
print '</ol>';

// Test 8. See if addServer actually pools the server resources
###############################################################
printHeader();

// Set up $conf so that both available servers map to default
$GLOBALS['conf']['memcache_servers'] = array('default' => array('localhost' => array(11211, 11212)),
);

// clear the $mc object
dmemcache_object('default', TRUE);


// make independent connections so we can display stats
$mc1 = new Memcache;
$mc1->connect('localhost', 11211);
$mc2 = new Memcache;
$mc2->connect('localhost', 11212);

// helper function specific to this test
function formatStats2($mc1, $mc2, $flush = FALSE) {
  static $count, $first;

  $stats1 = $mc1->getStats();
  $stats2 = $mc2->getStats();

  if ($flush) {
    unset($first);
  }

  if (!isset($first)) {
    $count = 1;
    $first = FALSE;
    print "<table border='1'><tr><th> </th><th>Server 1</th><th>Server 2</th></tr>";
  }
  print "<tr><td>$count</td><td>". $stats1['bytes']. "</td><td>". $stats2['bytes']. "</td></tr>";
  $count++;
}

$last_key = $last_value = '';
$time = time();
for ($i = 1; $i < 10001; $i++) {
  $last_key = $time.$i. 'key';
  $last_value = 'Some very random thoughts about things in general'. $time;
  dmemcache_set($last_key, $last_value, FALSE, 0);
  if ($i % 1000 == 1) {
    formatStats2($mc1, $mc2);
    flush();
  }
}

print "</table>";

// Test 9. Try using flush to clear servers
// Conclusion: It probably works, except that it doesn't actuall clear the memory.
// It only sets it as invalid so that it gets overwritten.
###########################################
printHeader();

formatStats2($mc1, $mc2, TRUE);
print "</table>";
dmemcache_flush();
formatStats2($mc1, $mc2, TRUE);
print "</table>";

// Test 10. See what the extended stats offer
#############################################
printHeader();

print "<pre>";
$types = array('reset', 'malloc', 'maps', 'slabs', 'items', 'sizes');
foreach ($types as $type) {
  print "<h2>$type</h2>";
  print "<h3>Server 1</h3>";
  print_r($mc1->getExtendedStats($type));
  print "<h3>Server 2</h3>";
  print_r($mc2->getExtendedStats($type));
}

print "</pre>";

// Test 11. Test delete.
########################
printHeader();

dmemcache_set('delete me', 'Goodbye world');
print "<h2>". dmemcache_get('delete me'). "</h2>";
dmemcache_delete('delete me');
print "<h2>Nothing here ---->". dmemcache_get('delete me'). "<--</h2>";



###################
// Helper functions
###################

function testKeyValue($mc, $key, $value) {
  $mc->set($key, $value, FALSE, 5);
  printKeyValue($key, $mc->get($key));
}

function dtestKeyValue($key, $value, $bin = 'default') {
  dmemcache_set($key, $value, 5, $bin);
  printKeyValue($key, dmemcache_get($key));
}

function printKeyValue($key, $value) {
  print '<li>'. $key. '='. $value. '</li>';
}

function formatStats($stats = array()) {
  print '<ul>';
  foreach ($stats as $name => $value) {
    print '<li>'. $name. '='. $value. '</li>';
  }
  print '</ul>';
}

function printHeader() {
  static $count = 1;
  print "<a name='$count'><h2>Test ". $count++. "</h2></a>";
}

?>