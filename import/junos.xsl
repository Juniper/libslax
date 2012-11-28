<?xml version="1.0" standalone="yes"?>
<!--
- $Id: junos.xsl,v 1.2 2012/02/29 12:17:49 jaiswalr Exp $
-
- Copyright (c) 2004-2008, Juniper Networks, Inc.
- All rights reserved.
-->

<xsl:stylesheet version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:junos="http://xml.juniper.net/junos/*/junos"
  xmlns:xnm="http://xml.juniper.net/xnm/1.1/xnm"
  xmlns:jcs="http://xml.juniper.net/junos/commit-scripts/1.0"
  xmlns:exsl="http://exslt.org/common"
  xmlns:ext="http://xmlsoft.org/XSLT/namespace">

  <!--
  - These parameters are passed into every commit script
  - by the system.
  -->
  <xsl:param name="hostname"/>
  <xsl:param name="localtime"/>
  <xsl:param name="localtime-iso"/>
  <xsl:param name="product"/>
  <xsl:param name="script"/>
  <xsl:param name="user"/>

  <xsl:variable name="junos-context"
      select="op-script-input/junos-context |
      commit-script-input/junos-context |
      event-script-input/junos-context"/>

  <xsl:output method="xml" indent="yes" standalone="yes"/>

  <!--
  - These are the default template rules, which allow normal
  - processing of commit script input data without having to
  - repeat these lines in every user script.  This should also
  - help if we ever need to change these templates.
  -
  - The first template generates the commit-script-output tag
  - and applies templates to the configuration data in the
  - commit script input.  The user script can then hold only
  - the template for match="configuration".
  -
  - The second template discards bare text nodes, which is
  - a normally xslt mechanism to allow an invocation of
  - apply-templates to recursively process the entire input
  - document.  This isn't so important for commit scripts,
  - but I've left it anyway.
  -->
  <xsl:template match="/">
  <!-- Commit Scripts - use /commit-script-input/configuration as context -->
    <xsl:choose>
      <xsl:when test="commit-script-input">
        <commit-script-results>
          <xsl:apply-templates select="commit-script-input/configuration"/>
        </commit-script-results>
      </xsl:when>
      <!-- Event Scripts - / will remain the context -->
      <xsl:when test="event-script-input">
        <event-script-results>
          <xsl:call-template name="junoscript"/>
        </event-script-results>
      </xsl:when>
      <!-- Op Scripts - / will remain the context -->
      <xsl:otherwise>
        <op-script-results>
          <xsl:call-template name="junoscript"/>
        </op-script-results>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- Discard any unprocessed text nodes -->
  <xsl:template match="text()"/>


  <!--
  - jcs:edit-path :: emit an 'edit-path' element
  - This template generates an 'edit-path' element
  - suitable for inclusion in an 'xnm:error' or 'xnm:warning'.
  - The location of the error is passed in as 'dot', which
  - defaults to '.', the current position in xml hierarchy.
  -->
  <xsl:template name="jcs:edit-path">
    <xsl:param name="dot" select="."/>
    <xsl:param name="xname">
      <xsl:if test="name($dot) != 'name'">
        <xsl:value-of select="name($dot)"/>
      </xsl:if>
    </xsl:param>
    <xsl:param name="rot"
               select="$dot[name() = $xname]
                       | $dot/../../*[name($dot) = 'name' and name = $dot]"/>

    <edit-path>
      <xsl:choose>
        <xsl:when test="jcs:empty($rot)">
          <xsl:text>[unknown location]</xsl:text>
        </xsl:when>
        <xsl:otherwise>
          <xsl:text>[edit</xsl:text>
          <xsl:call-template name="jcs:bare-edit-path">
            <xsl:with-param name="dot" select="$rot"/>
          </xsl:call-template>
          <xsl:text>]</xsl:text>
        </xsl:otherwise>
      </xsl:choose>
    </edit-path>
  </xsl:template>

  <!--
  - Used internally (recursively) by jcs:edit-path
  -->
  <xsl:template name="jcs:bare-edit-path">
    <xsl:param name="dot" select="."/>
    <xsl:param name="parent" select="$dot/parent::*"/>

    <xsl:if test="name($dot) != 'configuration'">
      <xsl:call-template name="jcs:bare-edit-path">
        <xsl:with-param name="dot" select="$parent"/>
      </xsl:call-template>
      <xsl:text> </xsl:text>
      <xsl:value-of select="name($dot)"/>
      <xsl:if test="$dot/name">
        <xsl:text> </xsl:text>
        <xsl:value-of select="$dot/name"/>
      </xsl:if>
    </xsl:if>
  </xsl:template>

  <!--
  - jcs:statement :: emit a 'statement' element
  - This template generates a 'statement' element suitable
  - for inclusion in an 'xnm:error' or 'xnm:warning'.  The
  - parameter 'dot' can be passed if the error is not at
  - the current position in the xml hierarchy.
  -->
  <xsl:template name="jcs:statement">
    <xsl:param name="dot" select="."/>
    <xsl:param name="dash" select="$dot | $dot/name"/>
    <xsl:param name="rot" select="$dash[last()]"/>
    <statement>
      <xsl:choose>
        <xsl:when test="name($rot) = 'name'">
          <xsl:value-of select="name($rot/parent::*)"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="name($rot)"/>
        </xsl:otherwise>
      </xsl:choose>
      <xsl:if test="$rot/text()">
        <xsl:text> </xsl:text>
        <xsl:value-of select="$rot"/>
      </xsl:if>
      <xsl:text>;</xsl:text>
    </statement>
  </xsl:template>

  <!--
  - jcs:emit-change :: emit a change with a parent hierarchy.
  - This template generates a 'change' element, which will
  - result in changes to the configuration.  The optional
  - 'message' parameter can contain a message to be displayed
  - to the user in an 'xnm:warning'.  The 'dot' parameter
  - allows the caller to indicate a location other than the
  - current location in the xml hierarchy.  The 'content' 
  - parameter should be the content of the change, relative
  - to 'dot'.  The 'tag' can be set to 'transient-change' if
  - a transient change is desired.
  -->
  <xsl:template name="jcs:emit-change">
    <xsl:param name="message"/>
    <xsl:param name="dot" select="."/>
    <xsl:param name="content"/>
    <xsl:param name="tag" select="'change'"/>
    <xsl:param name="name" select="name($dot)"/>
    
    <xsl:if test="$message">
      <xnm:warning>
        <xsl:call-template name="jcs:edit-path">
          <xsl:with-param name="dot" select="$dot"/>
        </xsl:call-template>  
        <message><xsl:copy-of select="$message"/></message>
      </xnm:warning>
    </xsl:if>

    <xsl:choose>
      <xsl:when test="not($name)">
        <xnm:error>
          <message>jcs:emit-change called with invalid location (dot)</message>
        </xnm:error>
      </xsl:when>
      <xsl:when test="$name = 'configuration'">
        <xsl:element name="{$tag}">
          <xsl:copy-of select="$content"/>
        </xsl:element>
      </xsl:when>
      <xsl:otherwise>
        <xsl:call-template name="jcs:emit-change">
          <xsl:with-param name="dot" select="$dot/.."/>
          <xsl:with-param name="tag" select="$tag"/>
          <xsl:with-param name="content">
            <xsl:element name="{$name}">
              <xsl:if test="$dot/name">
                <name><xsl:value-of select="$dot/name"/></name>
              </xsl:if>
              <xsl:copy-of select="$content"/>
            </xsl:element>
          </xsl:with-param>
        </xsl:call-template>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!--
  - jcs:emit-comment :: emit a simple comment
  - This template emits a simple comment that indicates a change
  - was made by a commit script.
  -->
  <xsl:template name="jcs:emit-comment">
    <junos:comment>
      <xsl:text>/* Added by user </xsl:text>
      <xsl:value-of select="$user"/>
      <xsl:text> on </xsl:text>
      <xsl:value-of select="$localtime"/>
      <xsl:text> (script </xsl:text>
      <xsl:value-of select="$script"/>
      <xsl:text>) */</xsl:text>
    </junos:comment>
  </xsl:template>

  <!--
  - jcs:grep($filename, $pattern) :: grep the pattern from the given file
  -->
  <xsl:template name="jcs:grep">
    <xsl:param name="filename"/>
    <xsl:param name="pattern"/>
    <xsl:variable name="query">
      <command>
        <xsl:value-of select="concat('file show ',  $filename)"/>
      </command>
    </xsl:variable>
    <xsl:variable name="out" select="jcs:invoke($query)"/>
    <!-- Read the file -->
    <xsl:variable name="lines" select="jcs:break_lines($out)"/>
    <xsl:for-each select="$lines">
      <xsl:variable name="res" select="jcs:regex($pattern, .)"/>
      <xsl:if test="$res[1]">
	<match>
          <input>
            <xsl:value-of select="."/>
          </input>
          <output>
            <xsl:value-of select="$res[1]"/>
          </output>
	</match>
      </xsl:if>
    </xsl:for-each>
  </xsl:template>

  <!-- 
- jcs:load-configuration($connection, $configuration, $action, $commit-options)
-    Template to load and commit the configuration from op-script
-    Arguments:
-         $connection     -> Junoscript session handler
-         $configuration  -> XML configuration
-         $action         -> merge|override|replace. Default: merge
-         $commit-options -> A node-set which contains commit options as
-			     synchronize|force-synchronize|full|check|log.
-			     Default: null
-         $rollback  ->      Index of rollback configuration file 
 -->
  <xsl:template name="jcs:load-configuration">
    <xsl:param name="connection"/>
    <xsl:param name="configuration"/>
    <xsl:param name="action" select="'merge'"/>
    <xsl:param name="commit-options" select="/.."/>
    <xsl:param name="rollback"/>
    <!-- Validate parameters -->
    <xsl:choose>
      <xsl:when test="not($connection)">
        <xnm:error>
          <message>jcs:load-configuration called with invalid parameters</message>
        </xnm:error>
      </xsl:when>
      <xsl:otherwise>
        <xsl:variable name="options-temp-1">
          <xsl:copy-of select="$commit-options"/>
        </xsl:variable>
        <xsl:variable name="options" select="ext:node-set($options-temp-1)"/>
        <xsl:variable name="commit-options-error-temp-2">
          <xsl:choose>
            <xsl:when test="$options/commit-options">
              <xsl:for-each select="$options/commit-options/node()">
                <xsl:variable name="node-name" select="name()"/>
                <xsl:if test="$node-name != 'synchronize' and $node-name != 'force-synchronize' and $node-name != 'full' and $node-name != 'check' and $node-name != 'log'">
                  <xsl:variable name="error-msg" select="concat('Commit option &quot;', $node-name, '&quot; is not supported')"/>
                  <xnm:error>
                    <message>
                      <xsl:value-of select="$error-msg"/>
                    </message>
                  </xnm:error>
                </xsl:if>
              </xsl:for-each>
            </xsl:when>
            <xsl:otherwise>
              <xsl:if test="$commit-options">
                <xnm:error>
                  <message>Commit options should be enclosed in &lt;commit-options&gt; tag</message>
                </xnm:error>
              </xsl:if>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:variable name="commit-options-error" select="ext:node-set($commit-options-error-temp-2)"/>
        <xsl:choose>
          <xsl:when test="jcs:empty($commit-options-error/*)">
            <xsl:variable name="lock-result" select="jcs:execute($connection, 'lock-configuration')"/>
            <!-- Emit warnings back -->
            <xsl:copy-of select="$lock-result/..//xnm:warning"/>
            <!-- Check for lock-configuration error -->
            <xsl:choose>
              <xsl:when test="$lock-result/..//xnm:error">
                <xsl:copy-of select="$lock-result"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:choose>
                  <xsl:when test="($rollback) and ($configuration)">
                    <xnm:error>
                      <message>jcs:load-configuration: Cannot pass valid configuration with rollback set</message>
                    </xnm:error>
                  </xsl:when>
                  <xsl:otherwise>
                    <xsl:variable name="load-config">
                      <xsl:choose>
                        <xsl:when test="($rollback)">
                          <load-configuration rollback="{$rollback}"/>
                        </xsl:when>
                        <xsl:otherwise>
                          <!-- Load the configuration -->
                          <xsl:if test="($configuration)">
                            <load-configuration format="xml" action="{$action}">
                              <xsl:copy-of select="$configuration"/>
                            </load-configuration>
                          </xsl:if>
                        </xsl:otherwise>
                      </xsl:choose>		    
                      <load-configuration format="xml" action="{$action}">
                        <xsl:copy-of select="$configuration"/>
                      </load-configuration>
                    </xsl:variable>
                    <xsl:if test="($rollback) or ($configuration)">
                      <xsl:variable name="load-result" select="jcs:execute($connection, $load-config)"/>
                      <xsl:choose>
                        <!-- Check for load-configuration error -->
                        <xsl:when test="$load-result/..//xnm:error">
                          <xsl:copy-of select="$load-result"/>
                        </xsl:when>
                        <xsl:otherwise>
                          <!-- Emit warnings back -->
		          <xsl:copy-of select="$load-result/..//xnm:warning"/>
                          <!-- Commit the configuration -->
                          <xsl:variable name="commit-config">
                            <commit-configuration>
                              <xsl:copy-of select="$options/commit-options/*"/>
                            </commit-configuration>
                          </xsl:variable>
                          <xsl:variable name="commit-result" select="jcs:execute($connection, $commit-config)"/>
                          <xsl:choose>		    
                            <!-- Check for commit-configuration error -->
                            <xsl:when test="$commit-result/..//xnm:error">
                              <xsl:copy-of select="$commit-result"/>
                            </xsl:when>
                            <xsl:otherwise>
                              <xsl:choose>
                                <!-- Check for commit succes -->
                                <xsl:when test="$commit-result/..//commit-success">
                                  <xsl:copy-of select="$commit-result"/>
                                </xsl:when>
                                <xsl:otherwise>
                                  <!-- Emit warnings back -->
                                  <xsl:copy-of select="$commit-result/..//xnm:warning"/>
                                </xsl:otherwise>
                              </xsl:choose>
                            </xsl:otherwise>
                          </xsl:choose>
                        </xsl:otherwise>
                      </xsl:choose>
                    </xsl:if>		      
                  </xsl:otherwise>
                </xsl:choose>
                <!-- Unlock database -->
                <xsl:variable name="unlock-result" select="jcs:execute($connection, 'unlock-configuration')"/>
                <!-- Emit warnings back -->
                <xsl:copy-of select="$unlock-result/..//xnm:warning"/>
                <xsl:if test="$unlock-result/..//xnm:error">
                  <xsl:copy-of select="$unlock-result"/>
                </xsl:if>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:when>
          <xsl:otherwise>
            <xsl:copy-of select="$commit-options-error"/>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>
</xsl:stylesheet>
