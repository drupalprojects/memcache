<?php

/**
 * @file
 * Contains \Drupal\memcache\Tests\MemcacheBackendUnitTest.
 */

namespace Drupal\memcache\Tests;

use Drupal\memcache\MemcacheBackendFactory;
use Drupal\system\Tests\Cache\GenericCacheBackendUnitTestBase;

/**
 * Tests the MemcacheBackend.
 */
class MemcacheBackendUnitTest extends GenericCacheBackendUnitTestBase {

  /**
   * Modules to enable.
   *
   * @var array
   */
  public static $modules = array('system', 'memcache');

  public static function getInfo() {
    return array(
      'name' => 'Memcache backend',
      'description' => 'Unit test of the memcache backend using the generic cache unit test base.',
      'group' => 'Cache',
    );
  }

  /**
   * Creates a new instance of DatabaseBackend.
   *
   * @return \Drupal\memcache\MemcacheBackend
   *   A new MemcacheBackend object.
   */
  protected function createCacheBackend($bin) {
    $factory = new MemcacheBackendFactory($this->container->get('lock'), $this->container->get('settings'), $this->container->get('state'), $this->container->get('memcache.factory'));
    return $factory->get($bin);
  }

}
