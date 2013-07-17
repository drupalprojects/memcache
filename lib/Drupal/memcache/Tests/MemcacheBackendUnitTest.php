<?php

/**
 * @file
 * Contains \Drupal\memcache\Tests\MemcacheBackendUnitTest.
 */

namespace Drupal\memcache\Tests;

use Drupal\system\Tests\Cache\GenericCacheBackendUnitTestBase;
use Drupal\memcache\MemcacheBackend;

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
   * @return
   *   A new DatabaseBackend object.
   */
  protected function createCacheBackend($bin) {
    return new MemcacheBackend($bin, $this->container->get('lock'), $this->container->get('settings'), $this->container->get('state'));
  }

}
