<?php
// $Id$

/*
 * Some unit tests used while developing. You may find them instructional.
 * Place the memcachetests.php file in the root Drupal directory. Start memcache servers
 * on localhost:11211 and localhost:11212
 */

// dummy implementations to keep memcache.inc happy
define('WATCHDOG_ERROR', 2);
function variable_get($name, $default) {
  return $default;
}
function watchdog($type, $message, $severity = WATCHDOG_NOTICE, $link = NULL) {
}


include_once './includes/memcache.inc';

print "<h1>Memcache configuration details</h1>";
print "<ul><li>memcache.allow_failover=". ini_get('memcache.allow_failover'). "</li>";
print "<li>memcache.max_failover_attempts=". ini_get('memcache.max_failover_attempts'). "</li>";
print "<li>memcache.chunk_size=". ini_get('memcache.chunk_size'). "</li>";
print "<li>memcache.default_port=". ini_get('memcache.default_port'). "</li>";
print "</ul>";

global $conf;

// by not defining any $conf for memcache we're essentially saying "give me localhost:11211"
// and only the 'default' bin will be active.

##############
// Begin Tests
##############

// Test 1. Connect to server and retrieve stats
###############################################
printHeader();
$mc = dmemcache_object();
formatStats($mc->getStats());
$mc->close();
unset($mc);

// Test 2. Set a number of keys and retrieve their values
#########################################################
printHeader();
$mc = dmemcache_object();
$keys = array('a', time(), 'http://www.robshouse.net/home/page?q=xyz&p=x', 'What about space?');

print '<ol>';
foreach ($keys as $key) {
  testKeyValue($mc, $key, 'Hi Robert');
}
print '</ol>';
$mc->close();
unset($mc);

// Test 3. Set a number of PROBLEMATIC keys and retrieve their values
#####################################################################
printHeader();

$mc = dmemcache_object();
print '<ol>';
$key = '   ';
testKeyValue($mc, $key, 'Hi Dude');

$key = "\n";
testKeyValue($mc, $key, 'Hi Dude 2');

print '<li><em>space and line break different?</em>='. $mc->get(' ').'</li>';
print '</ol>';
$mc->close();
unset($mc);

// Test 4. Test flushing the $mc object from dmemcache_object
#############################################################
printHeader();

$mc = dmemcache_object();
formatStats($mc->getStats());
unset($conf['memcache']);
$conf['memcache'][] = array(
  '#servers' => array('localhost:11212'),
  '#bins'    => array('default'),
);
$mc->close();
unset($mc);

$mc = dmemcache_object('default', TRUE);
formatStats($mc->getStats());
$mc->close();
unset($mc);


// Test 5. Confirm that space and line break are treated as the same character
##############################################################################
printHeader();
$mc = dmemcache_object();
$mc->set("\n", "This is a new line", FALSE, 5);
print '<ol>';
print '<li>'. $mc->get("\n"). '</li>';
print '<li>'. $mc->get(" "). '</li>';
print '</ol>';
$mc->close();
unset($mc);

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
unset($conf['memcache']);
$conf['memcache'][] = array(
  '#servers' => array('localhost:11211', 'localhost:11212'),
  '#bins'    => array('default'),
);


// clear the $mc object
dmemcache_object('default', TRUE);


// make independent connections so we can display stats. These will be used in the next 3 tests.
$mc1 = new Memcache;
$mc1->connect('localhost', 11211);
$mc2 = new Memcache;
$mc2->connect('localhost', 11212);

$last_key = $last_value = '';
$time = microtime();
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

// done with mc1 and mc2
$mc1->close();
$mc2->close();
unset($mc1);
unset($mc2);

// Test 11. Test delete.
########################
printHeader();

dmemcache_set('delete me', 'Goodbye world');
print "<h2>". dmemcache_get('delete me'). "</h2>";
dmemcache_delete('delete me');
print "<h2>Nothing here ---->". dmemcache_get('delete me'). "<--</h2>";

// Test 12. Save things to different bins
#########################################
printHeader();

$mc1 = new Memcache;
$mc1->connect('localhost', 11211);
$mc2 = new Memcache;
$mc2->connect('localhost', 11212);

// Set up two clusters with four bins
unset($conf['memcache']);
$conf['memcache'][] = array(
  '#servers' => array('localhost:11211'),
  '#bins'    => array('default', 'antwerp'),
);
$conf['memcache'][] = array(
  '#servers' => array('localhost:11212'),
  '#bins'    => array('vancouver', 'barcelona'),
);

// flush the cluster cache
dmemcache_object('', TRUE);

$first = TRUE;

print "<h3>Bin: default</h3>";
$last_key = $last_value = '';
$time = microtime();
for ($i = 1; $i < 10001; $i++) {
  $last_key = $time.$i. 'key';
  $last_value = 'default '. $i;
  dmemcache_set($last_key, $last_value, 20, 'default');
  if ($i % 1000 == 1) {
    $cluster = dmemcache_object('default');
    print_r($cluster);
    formatStats2($mc1, $mc2, $first);
    $first = FALSE;
    flush();
    $keyin_keyout[] = array('orig' => $last_value, 'cache' => dmemcache_get($last_key, 'default'));
  }
}
print "</table>";
print "<h4>Values for default</h4>";
print '<table border="1"><tr><th>Original</th><th>Cached</th></tr>';
foreach ($keyin_keyout as $values) {
  print "<tr><td>". $values['orig']. "</td><td>". $values['cache']. "</td></tr>";
}
print "</table>";

print "<h3>Bin: vancouver</h3>";
$first = TRUE;
$keyin_keyout = array();
$last_key = $last_value = '';
$time = microtime();
for ($i = 1; $i < 10001; $i++) {
  $last_key = $time.$i. 'key';
  $last_value = 'vancouver '. $i;
  dmemcache_set($last_key, $last_value, 20, 'vancouver');
  if ($i % 1000 == 1) {
    $cluster = dmemcache_object('vancouver');
    print_r($cluster);
    formatStats2($mc1, $mc2, $first);
    $first = FALSE;
    flush();
    $keyin_keyout[] = array('orig' => $last_value, 'cache' => dmemcache_get($last_key, 'vancouver'));
  }
}
print "</table>";
print "<h4>Values for vancouver</h4>";
print '<table border="1"><tr><th>Original</th><th>Cached</th></tr>';
foreach ($keyin_keyout as $values) {
  print "<tr><td>". $values['orig']. "</td><td>". $values['cache']. "</td></tr>";
}
print "</table>";

print "<h3>Bin: barcelona</h3>";
$first = TRUE;
$keyin_keyout = array();
$last_key = $last_value = '';
$time = microtime();
for ($i = 1; $i < 10001; $i++) {
  $last_key = $time.$i. 'key';
  $last_value = 'barcelona '. $i;
  dmemcache_set($last_key, $last_value, 20, 'barcelona');
  if ($i % 1000 == 1) {
    $cluster = dmemcache_object('barcelona');
    print_r($cluster);
    formatStats2($mc1, $mc2, $first);
    $first = FALSE;
    flush();
    $keyin_keyout[] = array('orig' => $last_value, 'cache' => dmemcache_get($last_key, 'barcelona'));
  }
}
print "</table>";
print "<h4>Values for barcelona</h4>";
print '<table border="1"><tr><th>Original</th><th>Cached</th></tr>';
foreach ($keyin_keyout as $values) {
  print "<tr><td>". $values['orig']. "</td><td>". $values['cache']. "</td></tr>";
}
print "</table>";


print "<h3>Bin: antwerp</h3>";
$first = TRUE;
$keyin_keyout = array();
$last_key = $last_value = '';
$time = microtime();
for ($i = 1; $i < 10001; $i++) {
  $last_key = $time.$i. 'key';
  $last_value = 'antwerp '. $i;
  dmemcache_set($last_key, $last_value, 20, 'antwerp');
  if ($i % 1000 == 1) {
    $cluster = dmemcache_object('antwerp');
    print_r($cluster);
    formatStats2($mc1, $mc2, $first);
    $first = FALSE;
    flush();
    $keyin_keyout[] = array('orig' => $last_value, 'cache' => dmemcache_get($last_key, 'antwerp'));
  }
}
print "</table>";
print "<h4>Values for antwerp</h4>";
print '<table border="1"><tr><th>Original</th><th>Cached</th></tr>';
foreach ($keyin_keyout as $values) {
  print "<tr><td>". $values['orig']. "</td><td>". $values['cache']. "</td></tr>";
}
print "</table>";

$mc1->close();
$mc2->close();
unset($mc1);
unset($mc2);

// Test 13. View the globaldebug messages
#########################################
printHeader();


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
?>
