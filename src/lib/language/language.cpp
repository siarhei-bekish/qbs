/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Build Suite.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "language.h"

#include "artifactproperties.h"
#include "scriptengine.h"
#include <buildgraph/artifact.h>
#include <buildgraph/productbuilddata.h>
#include <buildgraph/projectbuilddata.h>
#include <buildgraph/rulegraph.h> // TODO: Move to language?
#include <logging/translator.h>
#include <tools/hostosinfo.h>
#include <tools/error.h>
#include <tools/propertyfinder.h>
#include <tools/persistence.h>
#include <tools/qbsassert.h>

#include <QDir>
#include <QDirIterator>
#include <QMutexLocker>
#include <QScriptValue>

QT_BEGIN_NAMESPACE
inline QDataStream& operator>>(QDataStream &stream, qbs::Internal::JsImport &jsImport)
{
    stream >> jsImport.scopeName
           >> jsImport.fileNames
           >> jsImport.location;
    return stream;
}

inline QDataStream& operator<<(QDataStream &stream, const qbs::Internal::JsImport &jsImport)
{
    return stream << jsImport.scopeName
                  << jsImport.fileNames
                  << jsImport.location;
}
QT_END_NAMESPACE

namespace qbs {
namespace Internal {

/*!
 * \class FileTagger
 * \brief The \c FileTagger class maps 1:1 to the respective item in a qbs source file.
 */
void FileTagger::load(PersistentPool &pool)
{
    m_artifactExpression.setPattern(pool.idLoadString());
    pool.stream() >> m_fileTags;
}

void FileTagger::store(PersistentPool &pool) const
{
    pool.storeString(m_artifactExpression.pattern());
    pool.stream() << m_fileTags;
}

/*!
 * \class SourceArtifact
 * \brief The \c SourceArtifact class represents a source file.
 * Everything except the file path is inherited from the surrounding \c ResolvedGroup.
 * (TODO: Not quite true. Artifacts in transformers will be generated by the transformer, but are
 * still represented as source artifacts. We may or may not want to change this; if we do,
 * SourceArtifact could simply have a back pointer to the group in addition to the file path.)
 * \sa ResolvedGroup
 */
void SourceArtifact::load(PersistentPool &pool)
{
    pool.stream() >> absoluteFilePath;
    pool.stream() >> fileTags;
    properties = pool.idLoadS<PropertyMapInternal>();
}

void SourceArtifact::store(PersistentPool &pool) const
{
    pool.stream() << absoluteFilePath;
    pool.stream() << fileTags;
    pool.store(properties);
}

void SourceWildCards::load(PersistentPool &pool)
{
    prefix = pool.idLoadString();
    patterns = pool.idLoadStringList();
    excludePatterns = pool.idLoadStringList();
    pool.loadContainerS(files);
}

void SourceWildCards::store(PersistentPool &pool) const
{
    pool.storeString(prefix);
    pool.storeStringList(patterns);
    pool.storeStringList(excludePatterns);
    pool.storeContainer(files);
}

/*!
 * \class ResolvedGroup
 * \brief The \c ResolvedGroup class corresponds to the Group item in a qbs source file.
 */

 /*!
  * \variable ResolvedGroup::files
  * \brief The files listed in the group item's "files" binding.
  * Note that these do not include expanded wildcards.
  */

/*!
 * \variable ResolvedGroup::wildcards
 * \brief Represents the wildcard elements in this group's "files" binding.
 *  If no wildcards are specified there, this variable is null.
 * \sa SourceWildCards
 */

/*!
 * \brief Returns all files specified in the group item as source artifacts.
 * This includes the expanded list of wildcards.
 */
QList<SourceArtifactPtr> ResolvedGroup::allFiles() const
{
    QList<SourceArtifactPtr> lst = files;
    if (wildcards)
        lst.append(wildcards->files);
    return lst;
}

void ResolvedGroup::load(PersistentPool &pool)
{
    name = pool.idLoadString();
    pool.stream()
            >> enabled
            >> location;
    pool.loadContainerS(files);
    wildcards = pool.idLoadS<SourceWildCards>();
    properties = pool.idLoadS<PropertyMapInternal>();
}

void ResolvedGroup::store(PersistentPool &pool) const
{
    pool.storeString(name);
    pool.stream()
            << enabled
            << location;
    pool.storeContainer(files);
    pool.store(wildcards);
    pool.store(properties);
}

/*!
 * \class RuleArtifact
 * \brief The \c RuleArtifact class represents an Artifact item encountered in the context
 *        of a Rule item.
 * When applying the rule, one \c Artifact object will be constructed from each \c RuleArtifact
 * object. During that process, the \c RuleArtifact's bindings are evaluated and the results
 * are inserted into the corresponding \c Artifact's properties.
 * \sa Rule
 */
void RuleArtifact::load(PersistentPool &pool)
{
    Q_UNUSED(pool);
    pool.stream() >> fileName;
    pool.stream() >> fileTags;

    int i;
    pool.stream() >> i;
    bindings.clear();
    bindings.reserve(i);
    Binding binding;
    for (; --i >= 0;) {
        pool.stream() >> binding.name >> binding.code >> binding.location;
        bindings += binding;
    }
}

void RuleArtifact::store(PersistentPool &pool) const
{
    Q_UNUSED(pool);
    pool.stream() << fileName;
    pool.stream() << fileTags;

    pool.stream() << bindings.count();
    for (int i = bindings.count(); --i >= 0;) {
        const Binding &binding = bindings.at(i);
        pool.stream() << binding.name << binding.code << binding.location;
    }
}

/*!
 * \class PrepareScript
 * \brief The \c PrepareScript class represents the JavaScript code found in the "prepare" binding
 *        of a \c Rule or \c Transformer item in a qbs file.
 * \sa Rule
 * \sa ResolvedTransformer
 */

 /*!
  * \variable PrepareScript::script
  * \brief The actual Javascript code, taken verbatim from the qbs source file.
  */

  /*!
   * \variable PrepareScript::location
   * \brief The exact location of the script in the qbs source file.
   * This is mostly needed for diagnostics.
   */

void PrepareScript::load(PersistentPool &pool)
{
    pool.stream() >> script;
    pool.stream() >> location;
}

void PrepareScript::store(PersistentPool &pool) const
{
    pool.stream() << script;
    pool.stream() << location;
}

void ResolvedModule::load(PersistentPool &pool)
{
    name = pool.idLoadString();
    moduleDependencies = pool.idLoadStringList();
    setupBuildEnvironmentScript = pool.idLoadString();
    setupRunEnvironmentScript = pool.idLoadString();
    pool.stream() >> jsImports
      >> setupBuildEnvironmentScript
      >> setupRunEnvironmentScript;
}

void ResolvedModule::store(PersistentPool &pool) const
{
    pool.storeString(name);
    pool.storeStringList(moduleDependencies);
    pool.storeString(setupBuildEnvironmentScript);
    pool.storeString(setupRunEnvironmentScript);
    pool.stream() << jsImports
      << setupBuildEnvironmentScript
      << setupRunEnvironmentScript;
}

QString Rule::toString() const
{
    return QLatin1Char('[') + inputs.toStringList().join(QLatin1String(",")) + QLatin1String(" -> ")
            + outputFileTags().toStringList().join(QLatin1String(",")) + QLatin1Char(']');
}

FileTags Rule::outputFileTags() const
{
    FileTags result;
    foreach (const RuleArtifactConstPtr &artifact, artifacts)
        result.unite(artifact->fileTags);
    return result;
}

void Rule::load(PersistentPool &pool)
{
    script = pool.idLoadS<PrepareScript>();
    module = pool.idLoadS<ResolvedModule>();
    pool.stream() >> jsImports
        >> inputs
        >> usings
        >> explicitlyDependsOn
        >> multiplex;

    pool.loadContainerS(artifacts);
}

void Rule::store(PersistentPool &pool) const
{
    pool.store(script);
    pool.store(module);
    pool.stream() << jsImports
        << inputs
        << usings
        << explicitlyDependsOn
        << multiplex;

    pool.storeContainer(artifacts);
}

ResolvedProduct::ResolvedProduct()
    : enabled(true)
{
}

ResolvedProduct::~ResolvedProduct()
{
}

/*!
 * \brief Returns all files of all groups as source artifacts.
 * This includes the expanded list of wildcards.
 */
QList<SourceArtifactPtr> ResolvedProduct::allFiles() const
{
    QList<SourceArtifactPtr> lst;
    foreach (const GroupConstPtr &group, groups)
        lst += group->allFiles();
    return lst;
}

/*!
 * \brief Returns all files of all enabled groups as source artifacts.
 * \sa ResolvedProduct::allFiles()
 */
QList<SourceArtifactPtr> ResolvedProduct::allEnabledFiles() const
{
    QList<SourceArtifactPtr> lst;
    foreach (const GroupConstPtr &group, groups) {
        if (group->enabled)
            lst += group->allFiles();
    }
    return lst;
}

FileTags ResolvedProduct::fileTagsForFileName(const QString &fileName) const
{
    FileTags result;
    foreach (FileTaggerConstPtr tagger, fileTaggers) {
        if (FileInfo::globMatches(tagger->artifactExpression(), fileName)) {
            result.unite(tagger->fileTags());
        }
    }
    return result;
}

void ResolvedProduct::load(PersistentPool &pool)
{
    pool.stream()
        >> enabled
        >> fileTags
        >> additionalFileTags
        >> name
        >> targetName
        >> sourceDirectory
        >> destinationDirectory
        >> location;
    properties = pool.idLoadS<PropertyMapInternal>();
    pool.loadContainerS(rules);
    pool.loadContainerS(dependencies);
    pool.loadContainerS(fileTaggers);
    pool.loadContainerS(modules);
    pool.loadContainerS(groups);
    pool.loadContainerS(artifactProperties);
    buildData.reset(pool.idLoad<ProductBuildData>());
}

void ResolvedProduct::store(PersistentPool &pool) const
{
    pool.stream()
        << enabled
        << fileTags
        << additionalFileTags
        << name
        << targetName
        << sourceDirectory
        << destinationDirectory
        << location;

    pool.store(properties);
    pool.storeContainer(rules);
    pool.storeContainer(dependencies);
    pool.storeContainer(fileTaggers);
    pool.storeContainer(modules);
    pool.storeContainer(groups);
    pool.storeContainer(artifactProperties);
    pool.store(buildData.data());
}

QList<const ResolvedModule*> topSortModules(const QHash<const ResolvedModule*, QList<const ResolvedModule*> > &moduleChildren,
                                      const QList<const ResolvedModule*> &modules,
                                      QSet<QString> &seenModuleNames)
{
    QList<const ResolvedModule*> result;
    foreach (const ResolvedModule *m, modules) {
        if (m->name.isNull())
            continue;
        result.append(topSortModules(moduleChildren, moduleChildren.value(m), seenModuleNames));
        if (!seenModuleNames.contains(m->name)) {
            seenModuleNames.insert(m->name);
            result.append(m);
        }
    }
    return result;
}

static QScriptValue js_getenv(QScriptContext *context, QScriptEngine *engine)
{
    if (Q_UNLIKELY(context->argumentCount() < 1))
        return context->throwError(QScriptContext::SyntaxError,
                                   QLatin1String("getenv expects 1 argument"));
    QVariant v = engine->property("_qbs_procenv");
    QProcessEnvironment *procenv = reinterpret_cast<QProcessEnvironment*>(v.value<void*>());
    return engine->toScriptValue(procenv->value(context->argument(0).toString()));
}

static QScriptValue js_putenv(QScriptContext *context, QScriptEngine *engine)
{
    if (Q_UNLIKELY(context->argumentCount() < 2))
        return context->throwError(QScriptContext::SyntaxError,
                                   QLatin1String("putenv expects 2 arguments"));
    QVariant v = engine->property("_qbs_procenv");
    QProcessEnvironment *procenv = reinterpret_cast<QProcessEnvironment*>(v.value<void*>());
    procenv->insert(context->argument(0).toString(), context->argument(1).toString());
    return engine->undefinedValue();
}

enum EnvType
{
    BuildEnv, RunEnv
};

static QProcessEnvironment getProcessEnvironment(ScriptEngine *engine, EnvType envType,
                                                 const QList<ResolvedModuleConstPtr> &modules,
                                                 const PropertyMapConstPtr &productConfiguration,
                                                 TopLevelProject *project,
                                                 const QProcessEnvironment &env)
{
    QProcessEnvironment procenv = env;

    // Copy the environment of the platform configuration to the process environment.
    const QVariantMap &platformEnv = project->platformEnvironment;
    for (QVariantMap::const_iterator it = platformEnv.constBegin(); it != platformEnv.constEnd(); ++it)
        procenv.insert(it.key(), it.value().toString());

    QMap<QString, const ResolvedModule *> moduleMap;
    foreach (const ResolvedModuleConstPtr &module, modules)
        moduleMap.insert(module->name, module.data());

    QHash<const ResolvedModule*, QList<const ResolvedModule*> > moduleParents;
    QHash<const ResolvedModule*, QList<const ResolvedModule*> > moduleChildren;
    foreach (ResolvedModuleConstPtr module, modules) {
        foreach (const QString &moduleName, module->moduleDependencies) {
            const ResolvedModule * const depmod = moduleMap.value(moduleName);
            QBS_ASSERT(depmod, return env);
            moduleParents[depmod].append(module.data());
            moduleChildren[module.data()].append(depmod);
        }
    }

    QList<const ResolvedModule *> rootModules;
    foreach (ResolvedModuleConstPtr module, modules) {
        if (moduleParents.value(module.data()).isEmpty()) {
            QBS_ASSERT(module, return env);
            rootModules.append(module.data());
        }
    }

    {
        QVariant v;
        v.setValue<void*>(&procenv);
        engine->setProperty("_qbs_procenv", v);
    }

    engine->clearImportsCache();
    QScriptValue scope = engine->newObject();
    scope.setProperty("getenv", engine->newFunction(js_getenv, 1));
    scope.setProperty("putenv", engine->newFunction(js_putenv, 2));

    QSet<QString> seenModuleNames;
    QList<const ResolvedModule *> topSortedModules = topSortModules(moduleChildren, rootModules, seenModuleNames);
    foreach (const ResolvedModule *module, topSortedModules) {
        if ((envType == BuildEnv && module->setupBuildEnvironmentScript.isEmpty()) ||
            (envType == RunEnv && module->setupBuildEnvironmentScript.isEmpty() && module->setupRunEnvironmentScript.isEmpty()))
            continue;

        // handle imports
        engine->import(module->jsImports, scope, scope);

        // expose properties of direct module dependencies
        QScriptValue scriptValue;
        QVariantMap productModules = productConfiguration->value().value("modules").toMap();
        foreach (const ResolvedModule * const depmod, moduleChildren.value(module)) {
            scriptValue = engine->newObject();
            QVariantMap moduleCfg = productModules.value(depmod->name).toMap();
            for (QVariantMap::const_iterator it = moduleCfg.constBegin(); it != moduleCfg.constEnd(); ++it)
                scriptValue.setProperty(it.key(), engine->toScriptValue(it.value()));
            scope.setProperty(depmod->name, scriptValue);
        }

        // expose the module's properties
        QVariantMap moduleCfg = productModules.value(module->name).toMap();
        for (QVariantMap::const_iterator it = moduleCfg.constBegin(); it != moduleCfg.constEnd(); ++it)
            scope.setProperty(it.key(), engine->toScriptValue(it.value()));

        QString setupScript;
        if (envType == BuildEnv) {
            setupScript = module->setupBuildEnvironmentScript;
        } else {
            if (module->setupRunEnvironmentScript.isEmpty()) {
                setupScript = module->setupBuildEnvironmentScript;
            } else {
                setupScript = module->setupRunEnvironmentScript;
            }
        }

        QScriptContext *ctx = engine->currentContext();
        ctx->pushScope(scope);
        scriptValue = engine->evaluate(setupScript);
        ctx->popScope();
        if (Q_UNLIKELY(scriptValue.isError() || engine->hasUncaughtException())) {
            QString envTypeStr = (envType == BuildEnv ? "build" : "run");
            throw Error(QString("Error while setting up %1 environment: %2").arg(envTypeStr, scriptValue.toString()));
        }
    }

    engine->setProperty("_qbs_procenv", QVariant());
    return procenv;
}

void ResolvedProduct::setupBuildEnvironment(ScriptEngine *engine, const QProcessEnvironment &env) const
{
    if (!buildEnvironment.isEmpty())
        return;

    buildEnvironment = getProcessEnvironment(engine, BuildEnv, modules, properties,
                                             topLevelProject(), env);
}

void ResolvedProduct::setupRunEnvironment(ScriptEngine *engine, const QProcessEnvironment &env) const
{
    if (!runEnvironment.isEmpty())
        return;

    runEnvironment = getProcessEnvironment(engine, RunEnv, modules, properties,
                                           topLevelProject(), env);
}

const QList<RuleConstPtr> &ResolvedProduct::topSortedRules() const
{
    if (buildData->topSortedRules.isEmpty()) {
        FileTags productFileTags = fileTags;
        productFileTags += additionalFileTags;
        RuleGraph ruleGraph;
        ruleGraph.build(rules, productFileTags);
//        ruleGraph.dump();
        buildData->topSortedRules = ruleGraph.topSorted();
//        int i=0;
//        foreach (RulePtr r, m_topSortedRules)
//            qDebug() << ++i << r->toString() << (void*)r.data();
    }
    return buildData->topSortedRules;
}

TopLevelProject *ResolvedProduct::topLevelProject() const
{
     return project->topLevelProject();
}


ResolvedProject::ResolvedProject() : enabled(true), m_topLevelProject(0)
{
}

TopLevelProject *ResolvedProject::topLevelProject()
{
    if (m_topLevelProject)
        return m_topLevelProject;
    TopLevelProject *tlp = dynamic_cast<TopLevelProject *>(this);
    if (tlp) {
        m_topLevelProject = tlp;
        return m_topLevelProject;
    }
    QBS_CHECK(!parentProject.isNull());
    m_topLevelProject = parentProject->topLevelProject();
    return m_topLevelProject;
}

QList<ResolvedProjectPtr> ResolvedProject::allSubProjects() const
{
    QList<ResolvedProjectPtr> projectList = subProjects;
    foreach (const ResolvedProjectConstPtr &subProject, subProjects)
        projectList << subProject->allSubProjects();
    return projectList;
}

QList<ResolvedProductPtr> ResolvedProject::allProducts() const
{
    QList<ResolvedProductPtr> productList = products;
    foreach (const ResolvedProjectConstPtr &subProject, subProjects)
        productList << subProject->allProducts();
    return productList;
}

void ResolvedProject::load(PersistentPool &pool)
{
    name = pool.idLoadString();
    const QString fileName = pool.idLoadString();
    int line;
    int column;
    pool.stream() >> line;
    pool.stream() >> column;
    location = CodeLocation(fileName, line, column);
    pool.stream() >> enabled;

    int count;
    pool.stream() >> count;
    products.clear();
    products.reserve(count);
    for (; --count >= 0;) {
        ResolvedProductPtr rProduct = pool.idLoadS<ResolvedProduct>();
        if (rProduct->buildData) {
            foreach (Artifact * const a, rProduct->buildData->artifacts)
                a->product = rProduct;
        }
        products.append(rProduct);
    }

    pool.stream() >> count;
    subProjects.clear();
    subProjects.reserve(count);
    for (; --count >= 0;) {
        ResolvedProjectPtr p = pool.idLoadS<ResolvedProject>();
        subProjects.append(p);
    }

    pool.stream() >> m_projectProperties;
}

void ResolvedProject::store(PersistentPool &pool) const
{
    pool.storeString(name);
    pool.storeString(location.fileName());
    pool.stream() << location.line();
    pool.stream() << location.column();
    pool.stream() << enabled;
    pool.stream() << products.count();
    foreach (const ResolvedProductConstPtr &product, products)
        pool.store(product);
    pool.stream() << subProjects.count();
    foreach (const ResolvedProjectConstPtr &project, subProjects)
        pool.store(project);
    pool.stream() << m_projectProperties;
}


TopLevelProject::TopLevelProject()
{
}

TopLevelProject::~TopLevelProject()
{
}

QString TopLevelProject::deriveId(const QVariantMap &config)
{
    const QVariantMap qbsProperties = config.value(QLatin1String("qbs")).toMap();
    const QString buildVariant = qbsProperties.value(QLatin1String("buildVariant")).toString();
    const QString profile = qbsProperties.value(QLatin1String("profile")).toString();
    return profile + QLatin1Char('-') + buildVariant;
}

QString TopLevelProject::deriveBuildDirectory(const QString &buildRoot, const QString &id)
{
    return buildRoot + QLatin1Char('/') + id;
}

void TopLevelProject::setBuildConfiguration(const QVariantMap &config)
{
    m_buildConfiguration = config;
    m_id = deriveId(config);
}

QString TopLevelProject::buildGraphFilePath() const
{
    return ProjectBuildData::deriveBuildGraphFilePath(buildDirectory, id());
}

void TopLevelProject::store(const Logger &logger) const
{
    // TODO: Use progress observer here.

    if (!buildData)
        return;
    if (!buildData->isDirty) {
        logger.qbsDebug() << "[BG] build graph is unchanged in project " << id() << ".";
        return;
    }
    const QString fileName = buildGraphFilePath();
    logger.qbsDebug() << "[BG] storing: " << fileName;
    PersistentPool pool(logger);
    PersistentPool::HeadData headData;
    headData.projectConfig = buildConfiguration();
    pool.setHeadData(headData);
    pool.setupWriteStream(fileName);
    store(pool);
    buildData->isDirty = false;
}

void TopLevelProject::load(PersistentPool &pool)
{
    ResolvedProject::load(pool);
    pool.stream() >> m_id;
    pool.stream() >> platformEnvironment;
    pool.stream() >> usedEnvironment;
    QHash<QString, QString> envHash;
    pool.stream() >> envHash;
    for (QHash<QString, QString>::const_iterator i = envHash.begin(); i != envHash.end(); ++i)
        environment.insert(i.key(), i.value());
    buildData.reset(pool.idLoad<ProjectBuildData>());
    buildData->isDirty = false;
}

void TopLevelProject::store(PersistentPool &pool) const
{
    ResolvedProject::store(pool);
    pool.stream() << m_id;
    pool.stream() << platformEnvironment << usedEnvironment;
    QHash<QString, QString> envHash;
    foreach (const QString &key, environment.keys())
        envHash.insert(key, environment.value(key));
    pool.stream() << envHash;
    pool.store(buildData.data());
}

/*!
 * \class SourceWildCards
 * \brief Objects of the \c SourceWildCards class result from giving wildcards in a
 *        \c ResolvedGroup's "files" binding.
 * \sa ResolvedGroup
 */

/*!
  * \variable SourceWildCards::prefix
  * \brief Inherited from the \c ResolvedGroup
  * \sa ResolvedGroup
  */

/*!
 * \variable SourceWildCards::patterns
 * \brief All elements of the \c ResolvedGroup's "files" binding that contain wildcards.
 * \sa ResolvedGroup
 */

/*!
 * \variable SourceWildCards::excludePatterns
 * \brief Corresponds to the \c ResolvedGroup's "excludeFiles" binding.
 * \sa ResolvedGroup
 */

/*!
 * \variable SourceWildCards::files
 * \brief The \c SourceArtifacts resulting from the expanded list of matching files.
 */

QSet<QString> SourceWildCards::expandPatterns(const GroupConstPtr &group,
                                              const QString &baseDir) const
{
    QSet<QString> files = expandPatterns(group, patterns, baseDir);
    files -= expandPatterns(group, excludePatterns, baseDir);
    return files;
}

QSet<QString> SourceWildCards::expandPatterns(const GroupConstPtr &group,
        const QStringList &patterns, const QString &baseDir) const
{
    QSet<QString> files;
    foreach (QString pattern, patterns) {
        pattern.prepend(prefix);
        pattern.replace('\\', '/');
        QStringList parts = pattern.split(QLatin1Char('/'), QString::SkipEmptyParts);
        if (FileInfo::isAbsolute(pattern)) {
            QString rootDir;
            if (HostOsInfo::isWindowsHost()) {
                rootDir = parts.takeFirst();
                if (!rootDir.endsWith(QLatin1Char('/')))
                    rootDir.append(QLatin1Char('/'));
            } else {
                rootDir = QLatin1Char('/');
            }
            expandPatterns(files, group, parts, rootDir);
        } else {
            expandPatterns(files, group, parts, baseDir);
        }
    }

    return files;
}

void SourceWildCards::expandPatterns(QSet<QString> &result, const GroupConstPtr &group,
                                     const QStringList &parts,
                                     const QString &baseDir) const
{
    QStringList changed_parts = parts;
    bool recursive = false;
    QString part = changed_parts.takeFirst();

    while (part == QLatin1String("**")) {
        recursive = true;

        if (changed_parts.isEmpty()) {
            part = QLatin1String("*");
            break;
        }

        part = changed_parts.takeFirst();
    }

    const bool isDir = !changed_parts.isEmpty();

    const QString &filePattern = part;
    const QDirIterator::IteratorFlags itFlags = recursive
            ? QDirIterator::Subdirectories
            : QDirIterator::NoIteratorFlags;
    QDir::Filters itFilters = isDir
            ? QDir::Dirs
            : QDir::Files;

    if (isDir && !FileInfo::isPattern(filePattern))
        itFilters |= QDir::Hidden;
    if (filePattern != QLatin1String("..") && filePattern != QLatin1String("."))
        itFilters |= QDir::NoDotAndDotDot;

    QDirIterator it(baseDir, QStringList(filePattern), itFilters, itFlags);
    while (it.hasNext()) {
        const QString filePath = it.next();
        QBS_ASSERT(FileInfo(filePath).isDir() == isDir, break);
        if (isDir)
            expandPatterns(result, group, changed_parts, filePath);
        else
            result += QDir::cleanPath(filePath);
    }
}

} // namespace Internal
} // namespace qbs
